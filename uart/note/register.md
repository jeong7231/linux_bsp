# Register

## PL011 UART register offset
| 오프셋    | 이름     | 기능 설명                                                   |
| ------ | ------ | ------------------------------------------------------- |
| 0x00   | DR     | **Data Register** - 송수신 데이터 읽기/쓰기                       |
| 0x04   | RSRECR | **Receive Status/Error Clear** - 수신 에러 상태/에러 플래그 클리어    |
| 0x18   | FR     | **Flag Register** - FIFO 상태, 송수신 상태 비트                  |
| 0x24   | IBRD   | **Integer Baud Rate Divisor** - Baudrate의 정수부           |
| 0x28   | FBRD   | **Fractional Baud Rate Divisor** - Baudrate의 소수부        |
| 0x2C   | LCRH   | **Line Control Register** - 데이터 비트/패리티/FIFO 등 설정        |
| 0x30   | CR     | **Control Register** - UART 전체 제어 (enable, TX/RX 등)     |
| 0x34   | IFLS   | **Interrupt FIFO Level Select** - 인터럽트 발생 FIFO level 설정 |
| 0x38   | IMSC   | **Interrupt Mask Set/Clear** - 인터럽트 마스킹 설정              |
| 0x3C   | RIS    | **Raw Interrupt Status** - 마스킹 전 인터럽트 상태                |
| 0x40   | MIS    | **Masked Interrupt Status** - 마스킹 후 인터럽트 상태             |
| 0x44   | ICR    | **Interrupt Clear Register** - 인터럽트 클리어                 |
| 0x48   | DMACR  | **DMA Control Register** - DMA 송수신 제어                   |
| 0x80\~ | ITCR 등 | **Test/Debug Register** - 테스트 및 디버깅용                    |

- `DR` (0x00)
  - 송신 시 이 레지스터에 데이터를 쓰면 FIFO로 들어감
  - 수신 시 이 레지스터에 데이터를 읽으면 FIFO에서 데이터가 나옴
- `FR` (0x18)
  - FIFO 상태 및 송수신 가능 여부를 비트로 표시
    - `RXFE` : 수신 FIFO 비었는지
    - `TXFF` : 송신 FIFO 가득 찼는지
    - `TXFE` : 송신 FIFO 비었는지
    - `RXFF` : 수신 FIFO 가득 찼는지
- `IBRD` (0x24), `FBRD` (0x28)
  - Baudrate 설정
- `LCRH` (0x2C)
  - 데이터 길이(5~8bit), 패리티, FIFO 사용여부, 정지비트 등
- `CR` (0x30)
  - UART Enable, TX Enable, RX Enabel, Loopback 등 주요 기능 제어
- `ICR` (0x44)
  - 인터럽트 클리어, 특정 비트에 1을 쓰면 해당 인터럽트 플래그가 클리어됨