#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#define CLCD_IOC_MAGIC 'L'
#define CLCD_IOC_CLEAR  _IO(CLCD_IOC_MAGIC, 0)

int main() {
    int fd = open("/dev/clcd0", O_WRONLY);
    if (fd < 0) { perror("open"); return 1; }

    // LCD 전체 clear
    ioctl(fd, CLCD_IOC_CLEAR);

    // 이후 원하는 글자 출력
    write(fd, "Hello\nWorld", 11);

    close(fd);
    return 0;
}

