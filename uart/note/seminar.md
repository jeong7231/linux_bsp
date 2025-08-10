# UART3 디바이스 드라이버 발표 자료

## 1. UART 개요
UART(Universal Asynchronous Receiver/Transmitter)는 비동기 직렬 통신 장치로, 시작 비트와 정지 비트를 사용해 데이터 전송을 동기화한다. 클럭 신호 없이 송수신이 가능하며, 설정된 Baudrate에 따라 속도가 결정된다.

---

## 2. 코드 개요
- 하드웨어: Raspberry Pi 4 (BCM2711) PL011 기반 UART3
- 방식: 인터럽트 기반 송수신
- 버퍼: 송수신 각각 링버퍼 사용
- 디바이스 파일: `/dev/my_uart3`
- 테스트 프로그램: `my_uart3_app.c`

---

## 3. 레지스터 설정 상세

| 레지스터 | 주소 오프셋 | 설정 값 | 설정 의도 |
|----------|------------|---------|-----------|
| **UART_DR** (Data Register) | 0x00 | 동적 접근 | 송신 시 데이터 기록, 수신 시 데이터 읽기 |
| **UART_FR** (Flag Register) | 0x18 | 읽기 전용 | TXFF 비트로 송신 FIFO 상태 확인, RXFE 비트로 수신 FIFO 상태 확인 |
| **UART_IBRD** (Integer Baud Rate Divisor) | 0x24 | `IBRD = UARTCLK / (16 * baud)` | Baudrate 정수부 설정 (기본 115200bps) |
| **UART_FBRD** (Fractional Baud Rate Divisor) | 0x28 | `FBRD = ((UARTCLK % (16*baud))*64 + baud/2) / baud` | Baudrate 소수부 설정 |
| **UART_LCRH** (Line Control) | 0x2C | `UART_LCRH_FEN | UART_LCRH_WLEN_8` | FIFO 활성화(FEN), 데이터 길이 8비트(WLEN_8), 패리티 없음, 1 Stop bit |
| **UART_CR** (Control Register) | 0x30 | `UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE` (+ `UART_CR_LBE` 옵션) | UART Enable, 송수신 Enable, Loopback 모드 옵션 |
| **UART_IFLS** (Interrupt FIFO Level Select) | 0x34 | `UART_IFLS_HALF_RX | UART_IFLS_HALF_TX` | RX/TX 인터럽트 발생 시 FIFO 절반 기준 |
| **UART_IMSC** (Interrupt Mask Set/Clear) | 0x38 | `UART_IMSC_RXIM | UART_IMSC_RTIM` (TXIM은 필요 시) | RX 인터럽트와 RX Timeout 인터럽트 허용, TX 인터럽트는 송신 대기 시만 허용 |
| **UART_RIS** (Raw Interrupt Status) | 0x3C | 읽기 전용 | 마스크되지 않은 인터럽트 상태 확인 |
| **UART_MIS** (Masked Interrupt Status) | 0x40 | 읽기 전용 | 마스크 적용된 인터럽트 상태 확인 (ISR에서 판별) |
| **UART_ICR** (Interrupt Clear Register) | 0x44 | `UART_ICR_RXIC | UART_ICR_RTIC | UART_ICR_FEIC | UART_ICR_PEIC | UART_ICR_BEIC | UART_ICR_OEIC` (RX 관련) / `UART_ICR_TXIC` (TX 관련) | RX/TX 및 에러 상태 인터럽트 클리어 |
| **에러 클리어 비트** | FEIC, PEIC, BEIC, OEIC | 1로 기록 시 해당 에러 상태 플래그 해제 | 프레임, 패리티, 브레이크, 오버런 에러 초기화 |

---

## 4. 동작 플로우

### 4.1 초기화 (`my_uart3_init`)
1. `register_chrdev()`로 문자 디바이스 등록
2. `ioremap()`으로 MMIO 매핑
3. `request_irq()`로 인터럽트 핸들러 등록
4. 초기화 성공 메시지 출력

### 4.2 디바이스 오픈 (`my_uart3_open`)
1. `UART_CR` → 0x0 (UART 비활성화)
2. `UART_ICR` → 모든 인터럽트 클리어
3. `UART_IBRD` / `UART_FBRD` → Baudrate 계산 후 설정
4. `UART_LCRH` → FIFO Enable, 8N1 설정
5. `UART_IFLS` → RX/TX FIFO 절반 기준
6. `UART_IMSC` → RX, RX Timeout 인터럽트 허용
7. `UART_CR` → UART Enable, TX Enable, RX Enable (+ Loopback 옵션)

### 4.3 쓰기 (`my_uart3_write`)
- 사용자 버퍼 → 링버퍼 적재  
- FIFO 여유 시 직접 전송  
- 송신 데이터 대기 시 TX 인터럽트 활성화

### 4.4 읽기 (`my_uart3_read`)
- 링버퍼에서 데이터 추출  
- 사용자 공간으로 복사

### 4.5 인터럽트 핸들러 (`my_uart3_isr`)
- **RX 인터럽트**:  
  - FIFO에서 데이터 수신 → 링버퍼 저장  
  - RXIC, RTIC, FEIC, PEIC, BEIC, OEIC 클리어
- **TX 인터럽트**:  
  - 링버퍼 데이터 → FIFO 전송  
  - TXIC 클리어

### 4.6 종료 (`my_uart3_exit`)
1. 모든 인터럽트 마스크 및 클리어
2. `free_irq()`로 IRQ 해제
3. `iounmap()`으로 MMIO 해제
4. `unregister_chrdev()`로 디바이스 해제

---

## 5. 테스트 앱 (`my_uart3_app.c`)
1. `/dev/my_uart3` 열기
2. `"my_uart3 TEST\n"` 송신
3. 잠시 대기 (`usleep(100000)`)
4. 수신 데이터 읽어 출력
5. 디바이스 닫기

---

## 6. 요약
- PL011 UART3 하드웨어 직접 제어
- 모든 주요 레지스터와 비트 설정 의도 명확화
- 인터럽트 기반 송수신, 링버퍼로 데이터 관리
- 기본 설정: 8N1, 115200bps, FIFO Enable
