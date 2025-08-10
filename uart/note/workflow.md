# UART3 코드 워크플로우 정리 (디바이스 드라이버 · 애플리케이션)

## 1) 디바이스 드라이버 워크플로우

### A. 모듈 로드 초기화 (`module_init(my_uart3_init)`)
1. 파라미터 검증  
   - `irq >= 0`, `baudrate > 0` 확인.
2. 캐릭터 디바이스 등록  
   - `register_chrdev(0, "my_uart3", &my_uart3_fops)` → `major` 확보.
3. MMIO 매핑  
   - `ioremap(UART3_BASE_PHYS, UART3_REG_SIZE)` → `uart3_base`.
4. 인터럽트 핸들러 등록  
   - `request_irq(irq, my_uart3_isr, IRQF_SHARED, "my_uart3", &uart3_base)`.
5. 커널 로그  
   - 로드 정보 출력.

### B. 파일 오퍼레이션 바인딩
- `struct file_operations my_uart3_fops`  
  - `.open = my_uart3_open`  
  - `.read = my_uart3_read`  
  - `.write = my_uart3_write`  
  - `.release = my_uart3_release`

### C. 디바이스 오픈 경로 (`my_uart3_open`)
1. 하드웨어 정지 및 인터럽트 클리어  
   - `UART_CR ← 0x0`  
   - `UART_ICR ← 0x7FF` (모든 인터럽트/에러 래치 해제).
2. 보레이트 계산 및 적용  
   - `ibrd = UARTCLK / (16 * baudrate)`  
   - `fbrd = (((UARTCLK % (16*baudrate)) * 64 + baudrate/2) / baudrate)`  
   - `UART_IBRD ← ibrd`, `UART_FBRD ← fbrd`.
3. 프레이밍·FIFO·임계치 설정  
   - `UART_LCRH ← FEN | WLEN_8` (8N1, FIFO Enable).  
   - `UART_IFLS ← RX 1/2, TX 1/2`.
4. 인터럽트 마스크  
   - `UART_IMSC ← RXIM | RTIM` (수신·타임아웃만 우선 허용).  
   - TXIM은 송신 대기 발생 시에만 동적 설정.
5. UART 활성화  
   - `UART_CR ← UARTEN | TXE | RXE` (+ `LBE` 옵션 지원).  
   - 상태 로그 출력.

### D. 쓰기 경로 (`my_uart3_write`)
1. 사용자 공간 → 커널 1바이트씩 복사  
   - `copy_from_user(&ch, buf+i, 1)` 실패 시 즉시 반환.
2. TX 링버퍼 시도  
   - `spin_lock_irqsave(&txrb.lock)`  
   - 버퍼가 가득이면 잠금 해제 후 **직접 푸시** 경로로 분기.  
   - 여유가 있으면 `rb_put()` 하고 잠금 해제.
3. 직접 푸시(비블로킹 최적화)  
   - `UART_FR.TXFF`가 0이면 `UART_DR ← ch`.
   - FIFO가 가득이면 루프 종료(비블로킹).
4. 전송 킥  
   - `uart_tx_kick()` 호출로 HW FIFO 채우기 + TXIM 마스크 토글.
5. 반환  
   - 실제로 큐잉·직접전송된 바이트 수 `i` 반환.

### E. 읽기 경로 (`my_uart3_read`)
1. 최대 128바이트 로컬 버퍼  
   - `count` 상한 조정.
2. RX 링버퍼에서 당장 읽을 수 있는 만큼 팝  
   - `spin_lock_irqsave(&rxrb.lock)`  
   - `rb_get()` 반복.  
   - 0바이트면 `0` 반환(즉시 EOF 스타일, 폴링 상위 레벨이 재호출).
3. 사용자 공간 복사  
   - `copy_to_user(buf, kbuf, i)` 실패 시 `-EFAULT`.
4. 읽은 길이 `i` 반환.

### F. 인터럽트 서비스 루틴 (`my_uart3_isr`)
1. 원인 판별  
   - `mis = readl(UART_MIS)`.
2. RX 계열 처리 (`RXIM` 또는 `RTIM` 세트)  
   - `UART_FR.RXFE == 0` 동안 `UART_DR & 0xFF`를 반복 읽어 `rxrb`에 적재.  
   - 에러·RX 관련 인터럽트 동시 클리어  
     - `UART_ICR ← RXIC | RTIC | FEIC | PEIC | BEIC | OEIC`.
3. TX 계열 처리 (`TXIM` 세트)  
   - `uart_tx_kick()`로 TX 링버퍼 → HW FIFO 푸시.  
   - `UART_ICR ← TXIC`.
4. 결과 반환  
   - 처리 여부에 따라 `IRQ_HANDLED` 또는 `IRQ_NONE`.

### G. 송신 킥 (`uart_tx_kick`)
1. `txrb` 잠금.  
2. HW FIFO 여유가 있고 `txrb`에 데이터가 있으면 `UART_DR`로 최대한 푸시.  
3. 남은 데이터 유무에 따라 `UART_IMSC.TXIM` on/off.  
4. 잠금 해제.

### H. 파일 릴리스 (`my_uart3_release`)
- 특별 동작 없음. 하드웨어는 모듈 언로드까지 유지.

### I. 모듈 언로드 종료 (`module_exit(my_uart3_exit)`)
1. 인터럽트 마스크 전체 비활성화  
   - `UART_IMSC ← 0x0`, `UART_ICR ← 0x7FF`.
2. `free_irq(irq, &uart3_base)` 호출.  
3. `iounmap(uart3_base)`.  
4. `unregister_chrdev(major, "my_uart3")`.  
5. 언로드 로그.

### J. 동시성·버퍼링 포인트
- RX/TX 링버퍼 크기: 1024바이트. 2의 거듭제곱.  
- 인덱스 연산: `& (RB_SZ - 1)`로 wrap.  
- 락: `spin_lock_irqsave()`로 IRQ 컨텍스트에서도 안전.  
- 정책: **RXIM+RTIM 상시**, **TXIM 필요 시만**.  
- 기본 통신: 115200 8N1, FIFO 사용.


## 2) 애플리케이션 워크플로우 (`my_uart3_app.c`)

### A. 시작
1. 디바이스 열기  
   - `fd = open("/dev/my_uart3", O_RDWR)`  
   - 실패 시 `perror` 후 종료.

### B. 송신
1. 송신 버퍼 준비  
   - `send = "my_uart3 TEST\n"`.
2. 쓰기 호출  
   - `write(fd, send, strlen(send))`  
   - 드라이버는 데이터를 TX 링버퍼 또는 HW FIFO로 투입.  
   - 실패 시 에러 처리.

### C. 간격 대기
- `usleep(100000)` (100ms).  
- 목적: RX 타임아웃 인터럽트 유도 또는 반향 대기.  
- 실제 환경에서는 이벤트 기반(블로킹 read)이나 `select/poll`가 적합.

### D. 수신
1. `len = read(fd, recv, sizeof(recv)-1)`  
   - 드라이버의 RX 링버퍼에서 즉시 가용 데이터만 반환.  
   - 0이면 현재 읽을 데이터 없음.
2. 널 종료 후 출력  
   - `recv[len] = '\0'`  
   - `printf("Sent : %s\nRecv : %s\n", send, recv)`.

### E. 종료
- `close(fd)`.

### F. 동작 조건
- 루프백 시나리오  
  - 드라이버 모듈 파라미터 `loopback=1`이면 PL011 내부 루프백으로 **자체 에코** 가능.  
- 외부 에코 장치  
  - 별도 장치가 같은 속도/포맷으로 연결되어 에코를 반환해도 동작.  
- 블로킹 동작 조정  
  - 현재 드라이버 read는 즉시 반환 설계. 필요 시 블로킹/웨이크업 로직(대기 큐) 확장 가능.


## 3) 요약

- 드라이버  
  - `init → map → request_irq → fops 바인딩`  
  - `open에서 보레이트·프레이밍·IMSC·CR 설정`  
  - `write는 링버퍼 또는 직접 푸시 + TXIM 동적 on/off`  
  - `read는 RX 링버퍼에서 즉시 전달`  
  - `ISR은 MIS 판독 → RX 흡입/에러클리어 → TX 푸시/클리어`  
  - `exit에서 마스크·클리어 → free_irq → unmap → unregister`
- 앱  
  - `open → write → usleep → read → print → close`

