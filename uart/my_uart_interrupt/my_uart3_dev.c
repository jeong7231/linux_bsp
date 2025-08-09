#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>   // IRQ 처리를 위해 추가
#include <linux/wait.h>        // wait queue를 위해 추가

#define DEVICE_NAME "my_uart3"
#define UART3_BASE_PHYS 0xFE201600
#define UART3_REG_SIZE 0x90

#define CALC_IBRD(baud) ((UARTCLK) / (16 * (baud)))
#define CALC_FBRD(baud) ((((UARTCLK) % (16 * (baud))) * 64 + ((baud) / 2)) / (baud))

#define UARTCLK 48000000
#define BAUDRATE 115200

// PL011 UART registers offset
#define UART_DR 0x00
#define UART_FR 0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCRH 0x2C
#define UART_CR 0x30
#define UART_IMSC 0x38    // 인터럽트 마스크 레지스터 추가
#define UART_ICR 0x44

#define UART_FR_TXFF (1 << 5)
#define UART_FR_RXFE (1 << 4)
#define UART_IMSC_RXIM (1 << 4)  // RX 인터럽트 마스크 비트
#define UART_ICR_ALL 0x7FF

#define RX_BUF_SIZE 128

// 전역 변수들
static void __iomem *uart3_base;
static int major = -1;
static int irq_num = 51;  // BCM2711 UART IRQ 번호 (GIC 153)// 인터럽트 관련 변수들
static char rx_buf[RX_BUF_SIZE];
static int rx_count = 0;
static DECLARE_WAIT_QUEUE_HEAD(rx_waitq);

unsigned int ibrd = CALC_IBRD(BAUDRATE);
unsigned int fbrd = CALC_FBRD(BAUDRATE);

// IRQ 핸들러
static irqreturn_t uart3_irq_handler(int irq, void *dev_id)
{
    // RX FIFO에서 데이터 읽기
    while (!(readl(uart3_base + UART_FR) & UART_FR_RXFE)) {
        if (rx_count < RX_BUF_SIZE) {
            rx_buf[rx_count++] = readl(uart3_base + UART_DR) & 0xFF;
        } else {
            // 버퍼 가득 참 - 데이터 버리고 계속
            readl(uart3_base + UART_DR);
            break;
        }
    }
    
    // RX 인터럽트 클리어
    writel(UART_IMSC_RXIM, uart3_base + UART_ICR);
    
    // 대기 중인 프로세스 깨우기
    wake_up_interruptible(&rx_waitq);
    
    return IRQ_HANDLED;
}

// UART3 초기화
static int my_uart3_open(struct inode *inode, struct file *file)
{
    // 1. UART Disable
    writel(0x0, uart3_base + UART_CR);

    // 2. Clear interrupts
    writel(UART_ICR_ALL, uart3_base + UART_ICR);

    // 3. Baudrate 설정
    writel(ibrd, uart3_base + UART_IBRD);
    writel(fbrd, uart3_base + UART_FBRD);

    // 4. FIFO enable, 8bit, no parity, 1 stop
    writel((1 << 4) | (3 << 5), uart3_base + UART_LCRH);

    // 5. RX 인터럽트 활성화
    writel(UART_IMSC_RXIM, uart3_base + UART_IMSC);

    // 6. UART enable, TX/RX enable
    writel((1 << 0) | (1 << 8) | (1 << 9), uart3_base + UART_CR);

    pr_info("my_uart3: UART3 opened/initialized with interrupt\n");
    return 0;
}

static ssize_t my_uart3_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t i;
    char ch;

    for (i = 0; i < count; i++) {
        if (copy_from_user(&ch, buf + i, 1))
            return -EFAULT;

        // TX FIFO Full이면 대기
        while (readl(uart3_base + UART_FR) & UART_FR_TXFF)
            cpu_relax();

        writel(ch, uart3_base + UART_DR);
    }
    return count;
}

static ssize_t my_uart3_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    int copied;

    // 데이터가 없으면 대기 (인터럽트 기반)
    if (rx_count == 0) {
        if (wait_event_interruptible(rx_waitq, rx_count > 0))
            return -ERESTARTSYS;
    }

    // 요청한 크기와 버퍼에 있는 데이터 크기 중 작은 값 선택
    copied = (count < rx_count) ? count : rx_count;

    if (copy_to_user(buf, rx_buf, copied))
        return -EFAULT;

    // 버퍼 비우기 (간단한 구현)
    rx_count = 0;

    return copied;
}

static struct file_operations my_uart3_fops = {
    .owner = THIS_MODULE,
    .open = my_uart3_open,
    .read = my_uart3_read,
    .write = my_uart3_write,
};

static int __init my_uart3_init(void)
{
    int ret;

    // 1. 캐릭터 디바이스 등록
    major = register_chrdev(0, DEVICE_NAME, &my_uart3_fops);
    if (major < 0) {
        pr_err("my_uart3: char device register fail\n");
        return major;
    }

    // 2. MMIO 매핑
    uart3_base = ioremap(UART3_BASE_PHYS, UART3_REG_SIZE);
    if (!uart3_base) {
        unregister_chrdev(major, DEVICE_NAME);
        pr_err("my_uart3: UART3 MMIO ioremap fail\n");
        return -ENOMEM;
    }

    // 3. IRQ 등록 (직접 IRQ 번호 사용)
    ret = request_irq(irq_num, uart3_irq_handler, IRQF_SHARED, 
                      DEVICE_NAME, &uart3_base);
    if (ret) {
        pr_err("my_uart3: IRQ %d request failed: %d\n", irq_num, ret);
        iounmap(uart3_base);
        unregister_chrdev(major, DEVICE_NAME);
        return ret;
    }

    pr_info("my_uart3: loaded (major %d, IRQ %d)\n", major, irq_num);
    return 0;
}

static void __exit my_uart3_exit(void)
{
    // IRQ 해제
    free_irq(irq_num, &uart3_base);

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
MODULE_DESCRIPTION("Interrupt-based PL011 UART3 driver for BCM2711 (Raspberry Pi4)");