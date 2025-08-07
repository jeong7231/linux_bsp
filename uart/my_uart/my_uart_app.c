#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE "/dev/my_uart3"

int main()
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open device");
        return 1;
    }

    char send[] = "test uart3\n";
    char recv[100] = {0};

    // write
    if (write(fd, send, strlen(send)) < 0)
    {
        perror("Write failed");
        close(fd);
        return 1;
    }

    // 약간의 지연 (상대방 응답 기다림, 필요시 더 늘릴 수 있음)
    usleep(10000);

    // read
    int len = read(fd, recv, sizeof(recv) - 1);
    if (len < 0)
    {
        perror("Read failed");
        close(fd);
        return 1;
    }

    recv[len] = '\0';
    printf("Sent : %s\n", send);
    printf("Recv : %s\n", recv);

    close(fd);
    return 0;
}