
# Baudrate
## 1. 공식


$$
\boxed{
    \text{BaudDiv} = \frac{\text{UARTCLK}}{16 \times \text{Baudrate}}
}
$$

* **UARTCLK**: UART에 입력되는 클럭 주파수(라즈베리파이4 보통 48,000,000Hz)
* **Baudrate**: 원하는 통신 속도(bps, 예: 115200)
* **BaudDiv**: 분주값(정수부와 소수부로 나뉨)


## 2. 적용과정

1. **BaudDiv** 값을 계산

   * 예) UARTCLK=48,000,000, Baudrate=115200

$$
\text{BaudDiv} = \frac{48,000,000}{16 \times 115,200} = \frac{48,000,000}{1,843,200} \approx 26.0416667
$$

2. **정수부**는 `IBRD`(Integer Baud Rate Divisor)

   * IBRD = 26

3. **소수부**는 `FBRD`(Fractional Baud Rate Divisor)

   * FBRD 계산 공식:

$$
\text{FBRD} = \text{int}\left( (\text{BaudDiv 소수부}) \times 64 + 0.5 \right)
$$

   * 위 예시에서 소수부는 0.0416667
   * FBRD = int(0.0416667 × 64 + 0.5) = int(2.6667 + 0.5) = int(3.1667) = 3


## 3. 코드로 환산

### C 매크로

```c
#define UARTCLK 48000000
#define CALC_IBRD(baud)   ((UARTCLK) / (16 * (baud)))
#define CALC_FBRD(baud)   ((((UARTCLK) % (16 * (baud))) * 64 + ((baud)/2)) / (baud))
```

### 실제 값 대입 예

```c
unsigned int baud = 115200;
unsigned int ibrd = CALC_IBRD(baud);  // 26
unsigned int fbrd = CALC_FBRD(baud);  // 3
```

## 4. 레지스터에 적용

```c
writel(ibrd, uart3_base + UART_IBRD); // 정수부
writel(fbrd, uart3_base + UART_FBRD); // 소수부
```

## 5. 참고 (다른 보레이트 예시)

* **9600bps**

  * BaudDiv = 48000000 / (16 × 9600) ≈ 312.5
  * IBRD = 312
  * FBRD = int(0.5 × 64 + 0.5) = 32

## 6. 공식 요약

* **BaudDiv = UARTCLK / (16 × Baudrate)**
* **IBRD = BaudDiv의 정수부**
* **FBRD = int((BaudDiv의 소수부) × 64 + 0.5)**

