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

#define UARTCLK  48000000
#define BAUDRATE 115200
#define CALC_IBRD(baud) ((UARTCLK) / (16 * (baud)))
#define CALC_FBRD(baud) ((((UARTCLK) % (16 * (baud))) * 64 + ((baud) / 2)) / (baud))
static unsigned int ibrd = CALC_IBRD(BAUDRATE);
static unsigned int fbrd = CALC_FBRD(BAUDRATE);

static void __iomem *uart3_base;
static int major = -1;

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

#define UART_FR_TXFF (1 << 5)
#define UART_FR_RXFE (1 << 4)

#define UART_IMSC_RXIM (1 << 4)
#define UART_IMSC_TXIM (1 << 5)
#define UART_IMSC_RTIM (1 << 6)

#define UART_ICR_RXIC  (1 << 4)
#define UART_ICR_TXIC  (1 << 5)
#define UART_ICR_RTIC  (1 << 6)
#define UART_ICR_OEIC  (1 << 10)
#define UART_ICR_BEIC  (1 << 9)
#define UART_ICR_PEIC  (1 << 8)
#define UART_ICR_FEIC  (1 << 7)

#define RB_SZ 1024
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

#define UART3_IRQ_DEFAULT 50
static int irq = UART3_IRQ_DEFAULT;
module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "UART3 IRQ number (default 50, dmesg의 fe201600.serial irq)");

static void uart_tx_kick(void)
{
    unsigned long flags;
    spin_lock_irqsave(&txrb.lock, flags);
    while (!rb_empty(&txrb) && !(readl(uart3_base + UART_FR) & UART_FR_TXFF))
        writel(rb_get(&txrb), uart3_base + UART_DR);

    if (!rb_empty(&txrb))
        writel(readl(uart3_base + UART_IMSC) | UART_IMSC_TXIM, uart3_base + UART_IMSC);
    else
        writel(readl(uart3_base + UART_IMSC) & ~UART_IMSC_TXIM, uart3_base + UART_IMSC);

    spin_unlock_irqrestore(&txrb.lock, flags);
}

static irqreturn_t my_uart3_isr(int irqno, void *dev_id)
{
    u32 mis = readl(uart3_base + UART_MIS);
    int handled = 0;

    if (mis & (UART_IMSC_RXIM | UART_IMSC_RTIM)) {
        spin_lock(&rxrb.lock);
        while (!(readl(uart3_base + UART_FR) & UART_FR_RXFE)) {
            char c = readl(uart3_base + UART_DR) & 0xFF;
            if (!rb_full(&rxrb)) rb_put(&rxrb, c);
        }
        spin_unlock(&rxrb.lock);
        writel(UART_ICR_RXIC | UART_ICR_RTIC | UART_ICR_FEIC | UART_ICR_PEIC | UART_ICR_BEIC | UART_ICR_OEIC,
               uart3_base + UART_ICR);
        handled = 1;
    }

    if (mis & UART_IMSC_TXIM) {
        uart_tx_kick();
        writel(UART_ICR_TXIC, uart3_base + UART_ICR);
        handled = 1;
    }

    return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int my_uart3_open(struct inode *inode, struct file *file)
{
    writel(0x0, uart3_base + UART_CR);
    writel(0x7FF, uart3_base + UART_ICR);
    writel(ibrd, uart3_base + UART_IBRD);
    writel(fbrd, uart3_base + UART_FBRD);
    writel((1 << 4) | (3 << 5), uart3_base + UART_LCRH);    // FEN=1, WLEN=8bit
    writel((0x2 << 3) | (0x2 << 0), uart3_base + UART_IFLS);// RX/TX IFLS=1/2
    writel(UART_IMSC_RXIM | UART_IMSC_RTIM, uart3_base + UART_IMSC);
    writel((1 << 0) | (1 << 8) | (1 << 9), uart3_base + UART_CR); // UARTEN|TXE|RXE
    pr_info("my_uart3: UART3 opened/initialized\n");
    return 0;
}

static ssize_t my_uart3_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t i;
    unsigned long flags;
    char ch;

    for (i = 0; i < count; i++) {
        if (copy_from_user(&ch, buf + i, 1))
            return -EFAULT;

        spin_lock_irqsave(&txrb.lock, flags);
        if (rb_full(&txrb)) {
            spin_unlock_irqrestore(&txrb.lock, flags);
            if (!(readl(uart3_base + UART_FR) & UART_FR_TXFF)) {
                writel(ch, uart3_base + UART_DR);
                continue;
            }
            break; // 버퍼 가득. 비블로킹 반환.
        } else {
            rb_put(&txrb, ch);
            spin_unlock_irqrestore(&txrb.lock, flags);
        }
    }

    uart_tx_kick();
    return i;
}

static ssize_t my_uart3_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf[128];
    size_t i = 0;
    unsigned long flags;

    if (count > sizeof(kbuf)) count = sizeof(kbuf);

    spin_lock_irqsave(&rxrb.lock, flags);
    while (i < count && !rb_empty(&rxrb))
        kbuf[i++] = rb_get(&rxrb);
    spin_unlock_irqrestore(&rxrb.lock, flags);

    if (i == 0) return 0;
    if (copy_to_user(buf, kbuf, i)) return -EFAULT;
    return i;
}

static struct file_operations my_uart3_fops = {
    .owner = THIS_MODULE,
    .open  = my_uart3_open,
    .read  = my_uart3_read,
    .write = my_uart3_write,
};

static int __init my_uart3_init(void)
{
    if (irq < 0) return -EINVAL;

    major = register_chrdev(0, DEVICE_NAME, &my_uart3_fops);
    if (major < 0) return major;

    uart3_base = ioremap(UART3_BASE_PHYS, UART3_REG_SIZE);
    if (!uart3_base) {
        unregister_chrdev(major, DEVICE_NAME);
        return -ENOMEM;
    }

    if (request_irq(irq, my_uart3_isr, IRQF_SHARED, DEVICE_NAME, &uart3_base)) {
        iounmap(uart3_base);
        unregister_chrdev(major, DEVICE_NAME);
        return -EBUSY;
    }

    pr_info("my_uart3: loaded (major %d, irq %d)\n", major, irq);
    return 0;
}

static void __exit my_uart3_exit(void)
{
    if (uart3_base) {
        writel(0x0, uart3_base + UART_IMSC);
        writel(0x7FF, uart3_base + UART_ICR);
    }
    if (irq >= 0) free_irq(irq, &uart3_base);
    if (uart3_base) iounmap(uart3_base);
    if (major >= 0) unregister_chrdev(major, DEVICE_NAME);
    pr_info("my_uart3: unloaded\n");
}

module_init(my_uart3_init);
module_exit(my_uart3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JTY");
MODULE_DESCRIPTION("Interrupt-based PL011 UART3 driver for BCM2711 (Raspberry Pi4)");
