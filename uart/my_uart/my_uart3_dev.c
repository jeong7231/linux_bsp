#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "my_uart3"
#define UART3_BASE_PHYS 0xFE201600
#define UART3_REG_SIZE 0x90

static void __iomem *uart3_base;
static int major = -1;

// PL011 UART 레지스터 오프셋
#define UART_DR 0x00
#define UART_FR 0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCRH 0x2C
#define UART_CR 0x30
#define UART_ICR 0x44

#define UART_FR_TXFF (1 << 5)
#define UART_FR_RXFE (1 << 4)

// --- open: UART3 하드웨어 초기화 ---
static int my_uart3_open(struct inode *inode, struct file *file)
{
    // 1. UART Disable
    writel(0x0, uart3_base + UART_CR);

    // 2. Clear interrupts
    writel(0x7FF, uart3_base + UART_ICR);

    // 3. Baudrate: 115200 @ 48MHz UARTCLK
    writel(26, uart3_base + UART_IBRD);
    writel(3, uart3_base + UART_FBRD);

    // 4. FIFO enable, 8bit, no parity, 1 stop
    writel((1 << 4) | (3 << 5), uart3_base + UART_LCRH);

    // 5. UART enable, TX/RX enable
    writel((1 << 0) | (1 << 8) | (1 << 9), uart3_base + UART_CR);

    pr_info("my_uart3: UART3 opened/initialized\n");
    return 0;
}

static ssize_t my_uart3_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t i;
    char ch;

    for (i = 0; i < count; i++)
    {
        if (copy_from_user(&ch, buf + i, 1))
            return -EFAULT;

        while (readl(uart3_base + UART_FR) & UART_FR_TXFF)
            cpu_relax();

        writel(ch, uart3_base + UART_DR);
    }
    return count;
}

static ssize_t my_uart3_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf[128];
    size_t i;

    if (count > sizeof(kbuf))
        count = sizeof(kbuf);

    for (i = 0; i < count; i++)
    {
        if (readl(uart3_base + UART_FR) & UART_FR_RXFE)
            break;
        kbuf[i] = readl(uart3_base + UART_DR) & 0xFF;
    }

    if (i == 0)
        return 0;

    if (copy_to_user(buf, kbuf, i))
        return -EFAULT;

    return i;
}

static struct file_operations my_uart3_fops = {
    .owner = THIS_MODULE,
    .open = my_uart3_open,
    .read = my_uart3_read,
    .write = my_uart3_write,
};

static int __init my_uart3_init(void)
{
    major = register_chrdev(0, DEVICE_NAME, &my_uart3_fops);
    if (major < 0)
    {
        pr_err("my_uart3: char device register fail\n");
        return major;
    }

    uart3_base = ioremap(UART3_BASE_PHYS, UART3_REG_SIZE);
    if (!uart3_base)
    {
        unregister_chrdev(major, DEVICE_NAME);
        pr_err("my_uart3: UART3 MMIO ioremap fail\n");
        return -ENOMEM;
    }

    pr_info("my_uart3: loaded (major %d)\n", major);
    return 0;
}

static void __exit my_uart3_exit(void)
{
    if (uart3_base)
        iounmap(uart3_base);

    if (major >= 0)
        unregister_chrdev(major, DEVICE_NAME);

    pr_info("my_uart3: unloaded\n");
}

module_init(my_uart3_init);
module_exit(my_uart3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JTY");
MODULE_DESCRIPTION("uart3 device driver for bcm2711");
