# uart3 device driver

## 1. 전반적인 개요

* **목적**: `/dev/my_uart3`라는 디바이스 파일을 통해 사용자 공간(C 유저 앱)에서 UART3로 데이터를 송수신할 수 있도록 하는 문자 디바이스 드라이버
* **기능**:

  * UART 초기화 (`open`)
  * 송신 (`write`)
  * 수신 (`read`)
* **대상 SoC**: BCM2711 (Raspberry Pi 4)
* **커널 버전**: kernel 7L
* **접근 방식**: MMIO (Memory-Mapped I/O) 방식 사용, `ioremap()`을 통해 레지스터 접근

## 2. BCM2711의 UART3 주소 분석

### MMIO 주소

```c
#define UART3_BASE_PHYS 0xFE201600
```

이는 **BCM2711의 PL011 UART3 컨트롤러의 MMIO 주소**를 의미한다.

### 데이터시트 기반 주소 계산

* Bus address: 0x7E201600 (BCM2711 Peripheral Base 주소 기준)
* 실제 MMU(Memory Management Unit)에서는 이 주소를 0xFE201600으로 매핑
* 계산: `0x7E201600 - 0x7E000000 + 0xFE000000 = 0xFE201600`

## 3. 주요 레지스터 설명

| 레지스터   | 오프셋 | 설명                                            |
| ---------- | ------ | ----------------------------------------------- |
| UART\_DR   | 0x00   | Data Register (데이터 송수신)                   |
| UART\_FR   | 0x18   | Flag Register (TX Full, RX Empty 등 상태)       |
| UART\_IBRD | 0x24   | Integer Baud rate divisor                       |
| UART\_FBRD | 0x28   | Fractional Baud rate divisor                    |
| UART\_LCRH | 0x2C   | Line Control (데이터 포맷, FIFO 사용 등)        |
| UART\_CR   | 0x30   | Control Register (UART Enable, TX/RX Enable 등) |
| UART\_ICR  | 0x44   | Interrupt Clear Register                        |

## 4. 코드 동작 분석

### `my_uart3_init()`

* `register_chrdev()`로 문자 디바이스 등록
* `ioremap()`으로 UART3의 MMIO 물리 주소를 커널 가상 주소로 매핑
* 실패 시 unregister 및 오류 처리 수행

### `my_uart3_open()`

* UART CR(Control Register)을 0으로 초기화하여 UART 비활성화
* `UART_ICR`에 0x7FF를 써서 인터럽트 클리어
* Baud rate 설정: IBRD = 26, FBRD = 3
* `UART_LCRH` 설정: FIFO enable, 8-bit, no parity, 1 stop bit
* `UART_CR` 설정: UART enable, TX enable, RX enable

#### Baud Rate 계산

* 기준: 115200bps
* 공식: `BaudRate = UART_CLK / (16 * (IBRD + FBRD/64))`
* UART\_CLK가 3 MHz라면 → `3,000,000 / (16 * (26 + 3/64)) ≈ 115200`

### `my_uart3_write()`

* 유저 공간에서 한 바이트씩 복사 후 FIFO가 차 있지 않을 때 UART\_DR에 전송

```c
while (readl(uart3_base + UART_FR) & UART_FR_TXFF)
    cpu_relax();
writel(ch, uart3_base + UART_DR);
```

### `my_uart3_read()`

* FIFO가 비지 않은 동안 1바이트씩 UART\_DR에서 읽어 유저 공간으로 전달

```c
if (readl(uart3_base + UART_FR) & UART_FR_RXFE)
    break;
kbuf[i] = readl(uart3_base + UART_DR) & 0xFF;
```

## 5. 드라이버 등록 흐름

```c
module_init(my_uart3_init);
module_exit(my_uart3_exit);
```

* 커널에 모듈 삽입 시 `my_uart3_init()` 호출
* 제거 시 `my_uart3_exit()`에서 `iounmap()` 및 `unregister_chrdev()` 수행


