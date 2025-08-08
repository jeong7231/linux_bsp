#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DRIVER_AUTHOR        "JTY"
#define DRIVER_DESC          "Platform driver for BCM2711 UART3 (RPi4, custom attach)"
#define DRIVER_NAME          "my_uart3_platform"
#define CHRDEV_NAME          "my_uart3"
#define DT_COMPATIBLE        "jeong,my-uart3"

// PL011 UART 레지스터 오프셋 매크로
#define UART_DR      0x00
#define UART_FR      0x18
#define UART_IBRD    0x24
#define UART_FBRD    0x28
#define UART_LCRH    0x2C
#define UART_CR      0x30
#define UART_IMSC    0x38
#define UART_MIS     0x40
#define UART_ICR     0x44

#define UART_FR_TXFF   (1 << 5)
#define UART_FR_RXFE   (1 << 4)
#define UART_IMSC_RXIM (1 << 4)
#define UART_ICR_ALL   0x7FF

#define UARTCLK        48000000 // 48MHz (RPi4 기본)
#define BAUDRATE       115200

#define RX_BUF_SIZE    128

// RX FIFO 버퍼 및 상태
static void __iomem *uart3_base;
static int irq_num;
static int major;
static char rx_buf[RX_BUF_SIZE];
static int rx_count;
static DECLARE_WAIT_QUEUE_HEAD(rx_waitq);

// IRQ 핸들러
static irqreturn_t uart3_irq_handler(int irq, void *dev_id)
{
    while (!(readl(uart3_base + UART_FR) & UART_FR_RXFE)) {
        if (rx_count < RX_BUF_SIZE)
            rx_buf[rx_count++] = readl(uart3_base + UART_DR) & 0xFF;
        else
            break;
    }
    writel(UART_IMSC_RXIM, uart3_base + UART_ICR); // RX 인터럽트 클리어
    wake_up_interruptible(&rx_waitq);
    return IRQ_HANDLED;
}

// chrdev read/write
static ssize_t my_uart3_chrdev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    if (rx_count == 0)
        wait_event_interruptible(rx_waitq, rx_count > 0);

    if (copy_to_user(buf, rx_buf, rx_count))
        return -EFAULT;

    int copied = rx_count;
    rx_count = 0;
    return copied;
}
static ssize_t my_uart3_chrdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t i;
    char ch;
    for (i = 0; i < count; i++) {
        if (copy_from_user(&ch, buf + i, 1))
            return -EFAULT;
        while (readl(uart3_base + UART_FR) & UART_FR_TXFF);
        writel(ch, uart3_base + UART_DR);
    }
    return count;
}
static struct file_operations my_uart3_fops = {
    .owner  = THIS_MODULE,
    .read   = my_uart3_chrdev_read,
    .write  = my_uart3_chrdev_write,
};

// Device Tree 매칭
static const struct of_device_id my_uart3_of_match[] = {
    { .compatible = DT_COMPATIBLE },
    {},
};
MODULE_DEVICE_TABLE(of, my_uart3_of_match);

// probe
static int my_uart3_probe(struct platform_device *pdev)
{
    struct resource *res;
    int ret;
    void *dev_id = (void *)1;

    // MMIO 자동 할당
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    uart3_base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(uart3_base))
        return PTR_ERR(uart3_base);

    // IRQ 자동 할당
    irq_num = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, irq_num, uart3_irq_handler, 0, DRIVER_NAME, dev_id);
    if (ret)
        return ret;

    // chrdev 등록
    major = register_chrdev(0, CHRDEV_NAME, &my_uart3_fops);

    // UART3 하드웨어 설정
    unsigned int ibrd = UARTCLK / (16 * BAUDRATE);
    unsigned int fbrd = (((UARTCLK % (16 * BAUDRATE)) * 64) + (BAUDRATE / 2)) / BAUDRATE;

    writel(0x0,    uart3_base + UART_CR);            // Disable UART
    writel(UART_ICR_ALL, uart3_base + UART_ICR);     // Clear interrupts
    writel(ibrd,   uart3_base + UART_IBRD);          // IBRD
    writel(fbrd,   uart3_base + UART_FBRD);          // FBRD
    writel((1 << 4) | (3 << 5), uart3_base + UART_LCRH); // FIFO enable, 8bit
    writel(UART_IMSC_RXIM, uart3_base + UART_IMSC);  // RX interrupt enable
    writel((1 << 0) | (1 << 8) | (1 << 9), uart3_base + UART_CR); // Enable UART

    pr_info(DRIVER_NAME ": probe success (MMIO: %p, IRQ: %d, major: %d)\n", uart3_base, irq_num, major);

    return 0;
}

// remove
static void my_uart3_remove(struct platform_device *pdev)
{
    unregister_chrdev(major, CHRDEV_NAME);
    pr_info(DRIVER_NAME ": removed\n");
}

static struct platform_driver my_uart3_driver = {
    .probe  = my_uart3_probe,
    .remove = my_uart3_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = my_uart3_of_match,
    },
};
module_platform_driver(my_uart3_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JTY");
MODULE_DESCRIPTION("Platform driver for BCM2711 UART3 (RPi4, custom attach)");
