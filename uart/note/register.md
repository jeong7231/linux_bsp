# Baudrate 계산

## 1. 공식

$$
\boxed{
\text{BaudDiv} = \frac{\text{UARTCLK}}{16 \times \text{Baudrate}}
}
$$

* **UARTCLK**: UART에 입력되는 클럭 주파수 (예: **48 MHz**)
* **Baudrate**: 원하는 통신 속도 (bps, 예: 115 200)
* **BaudDiv**: 분주값 (정수부 + 소수부)


## 2. 적용 과정

1. **BaudDiv 계산**
   예) UARTCLK = **48 MHz**, Baudrate = 115 200bps

   $$
   \text{BaudDiv} = \frac{48\,000\,000}{16 \times 115\,200}
   = \frac{48\,000\,000}{1\,843\,200}
   \approx 26.0416667
   $$

2. **정수부 → IBRD (Integer Baud Rate Divisor)**

   $$
   \text{IBRD} = 26
   $$

3. **소수부 → FBRD (Fractional Baud Rate Divisor)**

   $$
   \text{FBRD} = \text{int}((0.0416667 \times 64) + 0.5)
   = \text{int}(2.6667 + 0.5)
   = 3
   $$


## 3. 계산 공식 (C 매크로)

```c
#define UARTCLK 48000000  // 48 MHz
#define CALC_IBRD(baud)   ((UARTCLK) / (16 * (baud)))
#define CALC_FBRD(baud)   ((((UARTCLK) % (16 * (baud))) * 64 + ((baud)/2)) / (baud))
```


## 4. 실제 값 예시

```c
unsigned int baud = 115200;
unsigned int ibrd = CALC_IBRD(baud);  // 26
unsigned int fbrd = CALC_FBRD(baud);  // 3
```


## 5. 레지스터 적용 예

```c
writel(ibrd, uart3_base + UART_IBRD); // 정수부
writel(fbrd, uart3_base + UART_FBRD); // 소수부
```

※ IBRD/FBRD는 **UART 비활성화 상태**에서 설정해야 함.


## 6. 다른 Baudrate 예시

| Baudrate (bps) | BaudDiv (48 MHz) | IBRD | FBRD |
| -------------: | ---------------: | ---: | ---: |
|        115 200 |       26.0416667 |   26 |    3 |
|         57 600 |       52.0833333 |   52 |    5 |
|         38 400 |           78.125 |   78 |    8 |
|         19 200 |           156.25 |  156 |   16 |
|          9 600 |            312.5 |  312 |   32 |

