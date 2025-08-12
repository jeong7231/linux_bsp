# 처음에 안됬던 이유
pl011을 내리고 내가만든 디바이스 드라이버를 적재하려함(irq = 50 으로)
근데 pl011은 빌트인 드라이버라 내려가지않음 `rmmod`안먹힘
그래서 공유인터럽트를 사용함 `IRQF_SHARED`
pl011을 내리고 uart3를 단독으로 사용하려면 built in으로 커널을 빌드할때 포함해야한다.


# 공유 인터럽트 사용
```c
ret = request_irq(irq, my_uart3_isr, IRQF_SHARED, DEVICE_NAME, &uart3_base);
```


## irq 50을 다른 UART가 점유중일때
`request_irq(50, ...)`

`IRQF_SHARED` 아님 → 바로 -EBUSY

`IRQF_SHARED`임 → 상대가 공유 미허용이면 -EBUSY


`IRQF_SHARED` 사용

- 해당 IRQ 라인이 다른 드라이버와 공유 가능.
- 실제 공유가 일어나려면, 동일 IRQ 번호를 잡는 모든 드라이버가 IRQF_SHARED로 요청해야 함.
- 공유 라인에서는 ISR이 호출될 때 원인 판별 후, 자기 장치 원인이 아니면 IRQ_NONE 반환해야 함.

# SPI
여기서 SPI는 **Serial Peripheral Interface**가 아니라, ARM GIC(Generic Interrupt Controller)의 **Shared Peripheral Interrupt**를 뜻한다.

1. 구분

* GIC에서 IRQ는 크게 두 가지 범주

  1. **SGI (Software Generated Interrupt)** – CPU 간 통신용, 번호 0\~15
  2. **PPI (Private Peripheral Interrupt)** – CPU별 로컬 장치용, 번호 16\~31
  3. **SPI (Shared Peripheral Interrupt)** – 모든 CPU가 공유하는 외부 장치용, 번호 32 이상
* BCM2711에서 UART, SPI 컨트롤러, I2C, GPIO 인터럽트 등은 SPI에 해당

2. 의미

* “SPI 50”이라고 하면, GIC에서 **ID 50번**에 해당하는 인터럽트 라인이라는 뜻
* 커널 드라이버가 `request_irq(50, ...)`를 호출하면 GIC의 SPI ID 50에 매핑된 하드웨어 라인을 요청하는 것

3. 연관

* Raspberry Pi4에서 UART3 → GIC SPI 50번으로 연결
* DT(Device Tree)에 `interrupts = <0 50 4>` 같은 형식으로 기재됨

  * 첫 값 0 = SPI, 두 번째 값 50 = ID, 세 번째 값 = 트리거 타입(예: level high)

원하는 장치의 IRQ를 쓰려면, 그 장치가 연결된 **정확한 SPI ID**를 요청해야 한다.
