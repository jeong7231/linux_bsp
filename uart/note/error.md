# Raspberry Pi4 UART3 인터럽트 동작 및 제약 정리

## 1. config.txt / Device Tree 상태
- `dtoverlay=disable-uart3` 적용됨
- `/proc/device-tree/soc/serial@7e201600/status` 값: `disabled`
- 커널은 UART3를 시스템에 등록하지 않음  
  → `/dev/ttyAMA3` 미생성, 기본 드라이버 attach 안 함


## 2. "Device or resource busy" 에러 원인
- `request_irq(50, ...)` 호출 실패
- IRQ 50번(GIC 153)이 시스템에 할당되지 않은 상태에서 직접 할당 시도
- `/proc/interrupts`에 `uart-pl011`이 보일 수 있으나, 실제 리소스는 해제되었거나 DT 반영이 불완전할 수 있음


## 3. Device Tree에서 "disabled" 의미
- 커널이 해당 하드웨어 노드를 드라이버에 전달하지 않음
- `/dev` 노드 생성 안 함
- `/proc/interrupts` 등록 안 함
- 다른 드라이버에서도 사용 불가
- 이 상태에서는 `ioremap` 가능하더라도 `request_irq()`는 허용되지 않음


## 핵심 결론
- Device Tree에서 `disabled` 된 리소스는 직접 `request_irq()` 호출로 점유 불가
- 실제 사용하려면 Device Tree에서 `status = "okay"` 로 활성화해야 함
- 커널이 활성화해야만 리소스(IRQ, MMIO)가 드라이버에 할당됨


## 인터럽트 방식 커널 모듈의 조건
- DT에서 노드를 `"okay"`로 설정
- 커널이 IRQ를 할당해야 `request_irq()` 성공
- `"disabled"` 상태에서는 항상 실패


## 라즈베리파이에서 커스텀 UART 드라이버 사용 방법

### 1. Polling 방식 사용
- `disabled` 상태에서도 MMIO 접근 가능
- 인터럽트 없이 FIFO를 직접 읽고 쓰는 방식
- 단점: CPU 점유율 증가

### 2. DT 활성화 후 기본 드라이버 attach 방지
- `status = "okay"`로 활성화
- 기본 PL011 드라이버가 바인딩되지 않도록 `compatible` 조정 또는 블랙리스트 처리
- 오버레이만으로는 구현 어려움, 커널/DT 빌드 필요


## 결론 및 추천
- Polling 방식이 동작한다면 그대로 사용하는 것이 현실적
- 인터럽트 방식 사용하려면 커널/Device Tree 수정 필요
- 공식 OS + overlay 환경에서는 인터럽트 기반 커스텀 모듈은 사실상 불가


## Raspberry Pi4 (BCM2711) UART별 IRQ 정보

| UART  | MMIO 주소       | GIC IRQ | 커널 IRQ 예시 |
|-------|----------------|---------|---------------|
| UART0 | 0xFE201000     | 81      | 32~60대       |
| UART1 | 0xFE215040     | 87      | 32~60대       |
| UART3 | 0xFE201600     | 153     | 50            |
| UART4 | 0xFE201800     | 154     | -             |
| UART5 | 0xFE201A00     | 155     | -             |

- GIC IRQ 번호는 하드웨어적으로 고정
- 커널 IRQ 번호는 내부 매핑 결과


## IRQ 변경 불가능성
- SoC 설계상 각 장치는 정해진 GIC IRQ 번호만 사용
- 남는 IRQ 번호를 임의로 장치에 할당할 수 없음
- UART3 → GIC IRQ 153 (커널 내부에서는 50번으로 매핑됨)


## 정리
- UART3는 GIC IRQ 153만 사용 가능
- IRQ 번호 변경 불가
- 커널 내부 IRQ 번호와 하드웨어 GIC IRQ는 1:1 매핑
