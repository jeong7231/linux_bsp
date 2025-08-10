// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#define DEVICE_NAME "my_uart3"
#define UART3_BASE_PHYS 0xFE201600
#define UART3_REG_SIZE  0x90

/* ---- Clock / baud ---- */
#define UARTCLK  48000000
static int baudrate = 115200;
module_param(baudrate, int, 0444);
MODULE_PARM_DESC(baudrate, "UART baudrate (default 115200)");

#define CALC_IBRD(baud) ((UARTCLK) / (16 * (baud)))
#define CALC_FBRD(baud) ((((UARTCLK) % (16 * (baud))) * 64 + ((baud) / 2)) / (baud))
static unsigned int ibrd;
static unsigned int fbrd;

/* ---- MMIO ---- */
static void __iomem *uart3_base;
static int major = -1;

/* ---- PL011 offsets ---- */
#define UART_DR    0x00
#define UART_FR    0x18
#define UART_IBRD  0x24
#define UART_FBRD  0x28
#define UART_LCRH  0x2C
#define UART_CR    0x30
#define UART_IFLS  0x34
#define UART_IMSC  0x38
#define UART_RIS   0x3C
#define UART_MIS   0x40
#define UART_ICR   0x44

/* ---- FR bits ---- */
#define UART_FR_TXFF (1 << 5)
#define UART_FR_RXFE (1 << 4)

/* ---- IMSC bits ---- */
#define UART_IMSC_RXIM (1 << 4)
#define UART_IMSC_TXIM (1 << 5)
#define UART_IMSC_RTIM (1 << 6)

/* ---- ICR bits ---- */
#define UART_ICR_RXIC  (1 << 4)
#define UART_ICR_TXIC  (1 << 5)
#define UART_ICR_RTIC  (1 << 6)
#define UART_ICR_OEIC  (1 << 10)
#define UART_ICR_BEIC  (1 << 9)
#define UART_ICR_PEIC  (1 << 8)
#define UART_ICR_FEIC  (1 << 7)

/* ---- CR bits ---- */
#define UART_CR_UARTEN (1 << 0)
#define UART_CR_LBE    (1 << 7)   /* internal loopback */
#define UART_CR_TXE    (1 << 8)
#define UART_CR_RXE    (1 << 9)

/* ---- LCRH bits ---- */
#define UART_LCRH_FEN    (1 << 4)
#define UART_LCRH_WLEN_8 (3 << 5)

/* ---- IFLS: RX/TX FIFO 1/2 ---- */
#define UART_IFLS_HALF_RX (0x2 << 3)
#define UART_IFLS_HALF_TX (0x2 << 0)

/* ---- Simple ring buffer ---- */
#define RB_SZ 1024 /* must be power of two */
struct ring {
	char buf[RB_SZ];
	unsigned int head, tail;
	spinlock_t lock;
};
static struct ring rxrb = { .lock = __SPIN_LOCK_UNLOCKED(rxrb.lock) };
static struct ring txrb = { .lock = __SPIN_LOCK_UNLOCKED(txrb.lock) };

static inline bool rb_empty(struct ring *r) { return r->head == r->tail; }
static inline bool rb_full(struct ring *r)  { return ((r->head + 1) & (RB_SZ - 1)) == r->tail; }
static inline void rb_put(struct ring *r, char c) { r->buf[r->head] = c; r->head = (r->head + 1) & (RB_SZ - 1); }
static inline char rb_get(struct ring *r) { char c = r->buf[r->tail]; r->tail = (r->tail + 1) & (RB_SZ - 1); return c; }

/* ---- IRQ ---- */
#define UART3_IRQ_DEFAULT 50
static int irq = UART3_IRQ_DEFAULT;
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "UART3 IRQ number (default 50)");

/* ---- Internal loopback toggle ---- */
static bool loopback;
module_param(loopback, bool, 0644);
MODULE_PARM_DESC(loopback, "Enable PL011 internal loopback (default false)");

static void uart_tx_kick(void)
{
	unsigned long flags;

	spin_lock_irqsave(&txrb.lock, flags);

	/* Push as much as possible into HW FIFO */
	while (!rb_empty(&txrb) && !(readl(uart3_base + UART_FR) & UART_FR_TXFF))
		writel(rb_get(&txrb), uart3_base + UART_DR);

	/* Arm or disarm TX interrupt based on pending data */
	if (!rb_empty(&txrb))
		writel(readl(uart3_base + UART_IMSC) | UART_IMSC_TXIM, uart3_base + UART_IMSC);
	else
		writel(readl(uart3_base + UART_IMSC) & ~UART_IMSC_TXIM, uart3_base + UART_IMSC);

	spin_unlock_irqrestore(&txrb.lock, flags);
}

static irqreturn_t my_uart3_isr(int irqno, void *dev_id)
{
	u32 mis = readl(uart3_base + UART_MIS);
	bool handled = false;

	/* RX or timeout */
	if (mis & (UART_IMSC_RXIM | UART_IMSC_RTIM)) {
		unsigned long flags;
		spin_lock_irqsave(&rxrb.lock, flags);
		while (!(readl(uart3_base + UART_FR) & UART_FR_RXFE)) {
			char c = readl(uart3_base + UART_DR) & 0xFF;
			if (!rb_full(&rxrb))
				rb_put(&rxrb, c);
		}
		spin_unlock_irqrestore(&rxrb.lock, flags);

		/* Clear RX-related sources and error latches */
		writel(UART_ICR_RXIC | UART_ICR_RTIC |
		       UART_ICR_FEIC | UART_ICR_PEIC | UART_ICR_BEIC | UART_ICR_OEIC,
		       uart3_base + UART_ICR);
		handled = true;
	}

	/* TX FIFO space available */
	if (mis & UART_IMSC_TXIM) {
		uart_tx_kick();
		writel(UART_ICR_TXIC, uart3_base + UART_ICR);
		handled = true;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/* ---- Char device fops ---- */
static int my_uart3_open(struct inode *inode, struct file *file)
{
	u32 cr;

	/* Disable and clear */
	writel(0x0,  uart3_base + UART_CR);
	writel(0x7FF, uart3_base + UART_ICR);

	/* Program baud and framing */
	ibrd = CALC_IBRD(baudrate);
	fbrd = CALC_FBRD(baudrate);
	writel(ibrd, uart3_base + UART_IBRD);
	writel(fbrd, uart3_base + UART_FBRD);
	writel(UART_LCRH_FEN | UART_LCRH_WLEN_8, uart3_base + UART_LCRH);
	writel(UART_IFLS_HALF_RX | UART_IFLS_HALF_TX, uart3_base + UART_IFLS);

	/* Enable RX + RX timeout interrupts now; TXIM is armed on demand */
	writel(UART_IMSC_RXIM | UART_IMSC_RTIM, uart3_base + UART_IMSC);

	/* Enable with optional internal loopback */
	cr = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
	if (loopback)
		cr |= UART_CR_LBE;
	writel(cr, uart3_base + UART_CR);

	pr_info("my_uart3: configured %d 8N1\n",
		baudrate);
	return 0;
}

static ssize_t my_uart3_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	size_t i;
	unsigned long flags;
	char ch;

	for (i = 0; i < count; i++) {
		if (copy_from_user(&ch, buf + i, 1))
			return i ? i : -EFAULT;

		spin_lock_irqsave(&txrb.lock, flags);
		if (rb_full(&txrb)) {
			spin_unlock_irqrestore(&txrb.lock, flags);

			/* Try direct push if HW FIFO not full */
			if (!(readl(uart3_base + UART_FR) & UART_FR_TXFF)) {
				writel(ch, uart3_base + UART_DR);
				continue;
			}
			/* Non-blocking: stop here */
			break;
		} else {
			rb_put(&txrb, ch);
			spin_unlock_irqrestore(&txrb.lock, flags);
		}
	}

	uart_tx_kick();
	return i;
}

static ssize_t my_uart3_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	char kbuf[128];
	size_t i = 0;
	unsigned long flags;

	if (count > sizeof(kbuf))
		count = sizeof(kbuf);

	spin_lock_irqsave(&rxrb.lock, flags);
	while (i < count && !rb_empty(&rxrb))
		kbuf[i++] = rb_get(&rxrb);
	spin_unlock_irqrestore(&rxrb.lock, flags);

	if (i == 0)
		return 0;
	if (copy_to_user(buf, kbuf, i))
		return -EFAULT;
	return i;
}

static int my_uart3_release(struct inode *inode, struct file *file)
{
	/* Nothing special; leave HW enabled until module unload */
	return 0;
}

static const struct file_operations my_uart3_fops = {
	.owner   = THIS_MODULE,
	.open    = my_uart3_open,
	.read    = my_uart3_read,
	.write   = my_uart3_write,
	.release = my_uart3_release,
};

/* ---- Module init/exit ---- */
static int __init my_uart3_init(void)
{
	int ret;

	if (irq < 0 || baudrate <= 0)
		return -EINVAL;

	major = register_chrdev(0, DEVICE_NAME, &my_uart3_fops);
	if (major < 0)
		return major;

	uart3_base = ioremap(UART3_BASE_PHYS, UART3_REG_SIZE);
	if (!uart3_base) {
		unregister_chrdev(major, DEVICE_NAME);
		return -ENOMEM;
	}

	ret = request_irq(irq, my_uart3_isr, IRQF_SHARED, DEVICE_NAME, &uart3_base);
	if (ret) {
		iounmap(uart3_base);
		unregister_chrdev(major, DEVICE_NAME);
		return ret;
	}

	pr_info("my_uart3: loaded (major %d, irq %d, base 0x%lx)\n",
		major, irq, (unsigned long)UART3_BASE_PHYS);
	return 0;
}

static void __exit my_uart3_exit(void)
{
	if (uart3_base) {
		/* Mask and clear all interrupts */
		writel(0x0, uart3_base + UART_IMSC);
		writel(0x7FF, uart3_base + UART_ICR);
	}

	if (irq >= 0)
		free_irq(irq, &uart3_base);

	if (uart3_base)
		iounmap(uart3_base);

	if (major >= 0)
		unregister_chrdev(major, DEVICE_NAME);

	pr_info("my_uart3: unloaded\n");
}

module_init(my_uart3_init);
module_exit(my_uart3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JTY, revised");
MODULE_DESCRIPTION("Interrupt-based PL011 UART3 driver for BCM2711 (Raspberry Pi 4)");
