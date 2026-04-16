---
name: atk-f407
description: |
  正点原子 探索者 STM32F407ZGT6 开发板 V3 完整硬件参考。
  用于 vibe coding 时快速了解引脚分配、外设映射、可用IO、冲突约束及 HAL 代码框架，
  确保生成代码引脚规划准确无误。
  调用场景：任何涉及该开发板的外设配置、引脚规划、HAL 驱动编写任务。
metadata:
  board: ATK-Explorer STM32F407 V3
  mcu: STM32F407ZGT6 (176-pin LQFP)
  clock: 168 MHz (HSE 8 MHz → PLL 336/2)
  flash: 1 MB internal + 16 MB W25Q128 (SPI)
  ram: 192 KB internal + 1 MB external SRAM (FSMC)
---

# ATK-Explorer STM32F407 V3 硬件参考

## 1. 核心参数

| 项目 | 值 |
|------|----|
| MCU | STM32F407ZGT6，Cortex-M4，168 MHz，FPU |
| 主晶振 | 8 MHz (OSC_IN/OSC_OUT) |
| RTC晶振 | 32.768 kHz (PC14/PC15，不可用作 GPIO) |
| 备用电源 | VBAT → CR1220（PC13 共享，不可用作 GPIO） |
| BOOT0 | 硬件控制（CH340C DTR/RTS 自动下载），BOOT1=PB2 |
| 下载口 | USART1 via CH340C（PA9/PA10，P10 跳线）|

---

## 2. 引脚分配总表

> **图例**：`●` = 专用已占用，不可复用  `○` = 条件可用（见备注）  `✓` = 自由可用
>
> "条件可用"指该引脚在不使用对应外设时或拔掉对应跳线帽后可当普通 IO 使用。

### PORT A

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PA0 | WK_UP / KEY_UP | ✓ | 高电平有效按键；不按下时可安全使用 |
| PA1 | RMII_REF_CLK (ETH) | ● | 直连 YT8512C TXC，50MHz 持续输出，不能用 |
| PA2 | USART2_TX / RS485_RX / ETH_MDIO | ○ | P4 跳线选 RS232 or RS485；拔 P4 可当 IO |
| PA3 | USART2_RX / RS485_TX / PWM_DAC | ○ | 同 PA2，拔 P4 可当 IO |
| PA4 | STM_DAC / DCMI_HREF | ✓ | DAC_OUT1；不用摄像头时完全自由 |
| PA5 | STM_ADC / TPAD | ✓ | ADC + 电容触摸键；P11 跳线，可自由使用 |
| PA6 | DCMI_PCLK | ✓ | 不用摄像头时完全自由 |
| PA7 | RMII_CRS_DV (ETH) | ● | 直连 YT8512C，不能用 |
| PA8 | DCMI_XCLK / REMOTE_IN / PWM_DAC | ○ | TIM1_CH1；LF0038 红外接收头（4.7K 上拉），可当 IO |
| PA9 | USART1_TX | ✓ | P10 跳线接 CH340C；可直接作 USART1 TX 使用 |
| PA10 | USART1_RX | ✓ | P10 跳线接 CH340C；可直接作 USART1 RX 使用 |
| PA11 | USB_D− / CAN_RX | ✓ | P5 跳线选 USB or CAN；**拔 P5 跳线帽**后可当 IO |
| PA12 | USB_D+ / CAN_TX | ✓ | 同 PA11 |
| PA13 | JTMS/SWDIO | ○ | SWD 调试用；不调试时可当 IO（内部 10K 上/下拉）|
| PA14 | JTCK/SWCLK | ○ | 同 PA13 |
| PA15 | JTDI / USB_PWR | ○ | USB HOST 供电控制；不用时可当 IO（10K 上拉）|

### PORT B

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PB0 | T_SCK（触摸屏 SPI SCK）| ✓ | 不接 LCD 触摸模块时完全自由 |
| PB1 | T_PEN（触摸屏中断）| ✓ | 同 PB0 |
| PB2 | BOOT1 / T_MISO | ○ | 上电 BOOT1（10K 下拉接地）；运行后可用；不用 LCD 触摸可当 IO |
| PB3 | JTDO / SPI1_SCK（W25Q128 + NRF24L01）| ● | SPI1 总线，禁止 Flash CS 时可用；不用 SPI 设备才可当 IO |
| PB4 | JTRST / SPI1_MISO | ● | 同 PB3 |
| PB5 | SPI1_MOSI | ● | 同 PB3 |
| PB6 | DCMI_D5 | ✓ | 不用摄像头时完全自由 |
| PB7 | DCMI_VSYNC | ✓ | 不用摄像头时完全自由 |
| PB8 | I2C1_SCL（24C02 / ES8388 / ST480MC）| ● | 4.7K 上拉；不用这些器件可当 IO，但上拉已焊 |
| PB9 | I2C1_SDA | ● | 同 PB8 |
| PB10 | USART3_TX / RS232_COM3_RX / ATK_RXD | ✓ | P2 跳线接 RS232/ATK 模块；**拔 P2** 后完全自由 |
| PB11 | USART3_RX / RS232_COM3_TX / ATK_TXD | ✓ | 同 PB10 |
| PB12 | I2S2_LRCK (ES8388) | ○ | 不用音频时可当 IO |
| PB13 | I2S2_SCLK (ES8388) | ○ | 不用音频时可当 IO |
| PB14 | W25Q128 片选 F_CS | ● | SPI Flash CS，不用 Flash 时才可当 IO |
| PB15 | LCD_BL（LCD 背光 PWM）| ✓ | 使用 LCD 时驱动背光，也可用作 PWM IO |

### PORT C

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PC0 | ATK_MODULE_LED | ○ | 不用 ATK 模块可当 IO |
| PC1 | ETH_MDC (YT8512C) | ● | 不用以太网时（PHY 复位状态）才可当 IO |
| PC2 | I2S_SDOUT (ES8388) | ○ | 不用音频时可当 IO |
| PC3 | I2S_SDIN (ES8388) | ○ | 不用音频时可当 IO |
| PC4 | RMII_RXD0 (ETH) | ● | 直连 YT8512C，不能用 |
| PC5 | RMII_RXD1 (ETH) | ● | 直连 YT8512C，不能用 |
| PC6 | I2S_MCLK / DCMI_D0 | ● | 音频 MCLK；不用音频+摄像头时可当 IO |
| PC7 | DCMI_D1 | ✓ | 不用摄像头时完全自由 |
| PC8 | SDIO_D0 / DCMI_D2 | ○ | 47K 上拉；不用 SD 卡和摄像头时可当 IO |
| PC9 | SDIO_D1 / DCMI_D3 | ○ | 同 PC8 |
| PC10 | SDIO_D2 | ○ | 47K 上拉；不用 SD 卡可当 IO |
| PC11 | SDIO_D3 | ○ | 47K 上拉；不用 SD 卡可当 IO |
| PC12 | SDIO_SCK / DCMI_D4 | ✓ | 不用 SD 卡和摄像头时完全自由 |
| PC13 | T_CS（触摸屏 SPI CS）| ✓ | 不接 LCD 触摸模块时完全自由 |
| PC14 | RTC 32.768kHz 晶振 | ● | **不可用作 GPIO** |
| PC15 | RTC 32.768kHz 晶振 | ● | **不可用作 GPIO** |

### PORT D

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PD0 | FSMC_D2 (LCD/SRAM) | ● | FSMC 数据总线，不可用 |
| PD1 | FSMC_D3 (LCD/SRAM) | ● | FSMC 数据总线，不可用 |
| PD2 | SDIO_CMD | ○ | 47K 上拉；不用 SD 卡可当 IO |
| PD3 | ETH_RESET (YT8512C) | ○ | 不用以太网时可当 IO |
| PD4 | FSMC_NOE / RD (LCD/SRAM) | ● | FSMC 控制，不可用 |
| PD5 | FSMC_NWE / WR (LCD/SRAM) | ● | FSMC 控制，不可用 |
| PD6 | DCMI_SCL (摄像头 SCCB) | ✓ | 不用摄像头时完全自由 |
| PD7 | DCMI_SDA (摄像头 SCCB) | ✓ | 不用摄像头时完全自由 |
| PD8 | FSMC_D13 | ● | 不可用 |
| PD9 | FSMC_D14 | ● | 不可用 |
| PD10 | FSMC_D15 | ● | 不可用 |
| PD11 | FSMC_A16 (SRAM) | ● | 不可用 |
| PD12 | FSMC_A17 (SRAM) | ● | 不可用 |
| PD13 | FSMC_A18 (SRAM) | ● | 不可用 |
| PD14 | FSMC_D0 | ● | 不可用 |
| PD15 | FSMC_D1 | ● | 不可用 |

### PORT E

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PE0 | FSMC_NBL0 (SRAM) | ● | 不可用 |
| PE1 | FSMC_NBL1 (SRAM) | ● | 不可用 |
| PE2 | KEY2（低有效）| ✓ | 普通按键输入 |
| PE3 | KEY1（低有效）| ✓ | 普通按键输入 |
| PE4 | KEY0（低有效）| ✓ | 普通按键输入 |
| PE5 | DCMI_D6 | ✓ | 不用摄像头时完全自由 |
| PE6 | DCMI_D7 | ✓ | 不用摄像头时完全自由 |
| PE7~PE15 | FSMC_D4~D12 | ● | FSMC 数据总线，全部不可用 |

### PORT F

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PF0~PF5 | FSMC_A0~A5 (SRAM) | ● | 不可用 |
| PF6 | ATK_MODULE_KEY | ✓ | 不用 ATK 模块时完全自由 |
| PF7 | 光敏传感器 ADC3_IN5 | ○ | 不用光敏传感器可当 IO；有外部分压电路 |
| PF8 | BEEP（蜂鸣器）| ○ | S8050 驱动，高电平响；可复用作普通 IO 输出 |
| PF9 | LED0 DS0（红，低有效）| ○ | 可复用，LED 也会同步亮灭 |
| PF10 | LED1 DS1（绿，低有效）| ○ | 可复用，LED 也会同步亮灭 |
| PF11 | T_MOSI（触摸屏 SPI）| ✓ | 不接 LCD 触摸模块时完全自由 |
| PF12 | FSMC_A6 (LCD RS / SRAM) | ● | 不可用（LCD 的 D/C 信号） |
| PF13~PF15 | FSMC_A7~A9 (SRAM) | ● | 不可用 |

### PORT G

| Pin | 功能 | 状态 | 备注 |
|-----|------|------|------|
| PG0~PG5 | FSMC_A10~A15 (SRAM) | ● | 不可用 |
| PG6 | NRF24L01 CE | ✓ | 不用 NRF 无线模块时完全自由 |
| PG7 | NRF24L01 CSN | ✓ | 不用 NRF 无线模块时完全自由 |
| PG8 | NRF24L01 IRQ / RS485 RE/DE | ○ | 同时控制 NRF IRQ 和 RS485 方向；不用两者可当 IO |
| PG9 | DCMI_PWDN / 1-Wire DQ | ○ | 4.7K 上拉；不用摄像头和单总线传感器可当 IO |
| PG10 | FSMC_NE3 (SRAM 片选) | ● | 不可用 |
| PG11 | RMII_TX_EN (ETH) | ● | 直连 YT8512C，不能用 |
| PG12 | FSMC_NE4 (LCD 片选) | ✓ | 使用 LCD 时占用；不用 LCD 可当 IO |
| PG13 | RMII_TXD0 (ETH) | ● | 不能用 |
| PG14 | RMII_TXD1 (ETH) | ● | 不能用 |
| PG15 | DCMI_RESET | ✓ | 不用摄像头时完全自由 |

---

## 3. 常用外设快查

| 外设 | 引脚 | 芯片/接口 |
|------|------|-----------|
| **LED0(红)** | PF9 低有效 | — |
| **LED1(绿)** | PF10 低有效 | — |
| **KEY0** | PE4 低有效 | — |
| **KEY1** | PE3 低有效 | — |
| **KEY2** | PE2 低有效 | — |
| **KEY_UP(WK_UP)** | PA0 高有效 | — |
| **蜂鸣器** | PF8 高有效 | S8050 NPN |
| **LCD 背光** | PB15 高/PWM | — |
| **LCD FSMC** | NE4=PG12, RS=PF12(A6), RD=PD4, WR=PD5, D0-D15=PD/PE | FSMC Bank1 NE4 |
| **LCD 基地址** | `0x6C000000`（写RS=0） `0x6C000000+0x100`（写RS=1，A6偏移）||
| **触摸屏 SPI** | SCK=PB0, MISO=PB2, MOSI=PF11, CS=PC13, PEN=PB1 | XPT2046/GT系列 |
| **USART1** | TX=PA9, RX=PA10 | CH340C USB转串口(P10跳线) |
| **USART2** | TX=PA2, RX=PA3 + RS485 RE/DE=PG8 | RS485(P4跳线)/RS232-COM2 |
| **USART3** | TX=PB10, RX=PB11 | RS232-COM3(P2跳线)/ATK模块 |
| **SPI1** | SCK=PB3, MISO=PB4, MOSI=PB5 | W25Q128 CS=PB14; NRF24L01 CE=PG6 CSN=PG7 IRQ=PG8 |
| **I2C1** | SCL=PB8, SDA=PB9 | 24C02 EEPROM / ES8388音频 |
| **外部SRAM** | FSMC Bank1 NE3=PG10, A0-A18=PF/PG/PD | XM8A51216 1MB 基址`0x68000000` |
| **SD卡 SDIO** | D0-D3=PC8-PC11, SCK=PC12, CMD=PD2 | — |
| **W25Q128 Flash** | SPI1 + CS=PB14 | 16MB |
| **以太网 ETH** | RMII: PA1,PA7,PC1,PC4,PC5,PG11,PG13,PG14 + RST=PD3 + MDIO=PA2(共享) | YT8512C |
| **CAN1** | RX=PA11, TX=PA12（P5跳线，与USB互斥）| SIT1050T |
| **USB_FS** | DM=PA11, DP=PA12（P5跳线，与CAN互斥）| — |
| **I2S / 音频** | MCLK=PC6, SCLK=PB13, LRCK=PB12, SDIN=PC3, SDOUT=PC2 | ES8388, I2C控制=PB8/PB9 |
| **摄像头 DCMI** | D0-D7=PC6,PC7,PC8,PC9,PC11,PB6,PE5,PE6; PCLK=PA6,VSYNC=PB7,HREF=PA4,XCLK=PA8; SCCB=PD6/PD7; RST=PG15; PWDN=PG9 | OV2640 |
| **蜂鸣器** | PF8 | — |
| **光敏传感器** | PF7 → ADC3_IN5 | — |
| **单总线传感器** | PG9 (4.7K上拉) | DHT11 / DS18B20 |
| **红外接收** | PA8 → TIM1_CH1 | LF0038 |
| **NRF24L01** | SPI1 + CE=PG6, CSN=PG7, IRQ=PG8 | — |
| **TPAD电容触摸** | PA5 | RC充放电 |
| **ADC测试端口** | PA5 | ADC1/2/3 IN5 |
| **DAC测试端口** | PA4 | DAC_OUT1 |
| **ATK模块接口** | KEY=PF6, LED=PC0, RXD=PB10, TXD=PB11 | 蓝牙/GPS等 |

---

## 4. 关键冲突约束

```
⚠️  PA11/PA12 = USB_DM/DP ↔ CAN1_RX/TX  — 由 P5 跳线帽选择，二选一
⚠️  PA2       = USART2_TX ↔ ETH_MDIO     — P4 决定接 RS485/RS232 还是以太网
⚠️  PA8       = DCMI_XCLK ↔ IR_IN        — 红外和摄像头时钟共引脚
⚠️  PG8       = NRF_IRQ   ↔ RS485_RE/DE  — 同时控制，不可同时启用
⚠️  PG9       = DCMI_PWDN ↔ 1-Wire DQ   — 摄像头与单总线传感器二选一
⚠️  PB10/PB11 = USART3    ↔ ATK_MODULE  — P2 跳线帽选择
⚠️  PC6       = I2S_MCLK  ↔ DCMI_D0     — 音频与摄像头 D0 冲突
⚠️  PC8~PC11  = SDIO_D0~D3 ↔ DCMI_D2~D4 — SD卡与摄像头冲突
⚠️  PB3~PB5   = SPI1共享   — W25Q128 和 NRF24L01 共用总线，用 CS 区分
⚠️  PC14/PC15 = RTC晶振    — 永远不可当 GPIO
⚠️  FSMC引脚   — PD0-D1/D8-D15, PE7-PE15, PF0-F5/F12-F15, PG0-G5/G10/G12 全部已占用
```

---

## 5. 推荐空闲 IO（用于新增外设）

> 以下为**无外部硬件冲突**、可直接用作新增外设的 GPIO，优先选用：

| 优先级 | 引脚 | 可用前提 | 推荐用途 |
|--------|------|----------|----------|
| ★★★ | PB6, PB7 | 不用摄像头 | 普通 GPIO / UART / TIM |
| ★★★ | PC7, PC12 | 不用摄像头和SD卡 | 普通 IO |
| ★★★ | PD6, PD7 | 不用摄像头 | 普通 IO / 软 I2C |
| ★★★ | PG6, PG7 | 不用 NRF24L01 | 普通 IO |
| ★★★ | PG15 | 不用摄像头 | 普通 IO |
| ★★★ | PE5, PE6 | 不用摄像头 | 普通 IO |
| ★★ | PA11, PA12 | 拔 P5 跳线帽 | USB 或 CAN 或普通 IO |
| ★★ | PB10, PB11 | 拔 P2 跳线帽 | **USART3** 最常用（直连 MCU，无电平转换）|
| ★★ | PA2, PA3 | 拔 P4 跳线帽 | USART2 |
| ★ | PF6, PC0 | 不用 ATK 模块 | 普通 IO |

> **推荐 USART 扩展首选**：**USART3 (PB10/PB11)**，拔掉 P2 跳线帽，
> 直接 MCU-to-MCU 3.3V LVTTL，无需任何电平转换芯片。

---

## 6. FSMC 地址空间

```c
/* LCD  — FSMC Bank1 NE4  (A6 = RS 信号) */
#define LCD_BASE_CMD   ((uint32_t)0x6C000000)        /* RS=0: 写命令/读数据 */
#define LCD_BASE_DATA  ((uint32_t)0x6C000000 | (1<<(6+1))) /* RS=1: 写数据 (A6偏移) */
/* 实际地址: 0x6C000000 + 0x80 = 0x6C000080 (16-bit总线，HADDR[6+1]对应A6) */

/* SRAM — FSMC Bank1 NE3  (XM8A51216, 1MB, 16-bit) */
#define SRAM_BASE      ((uint32_t)0x68000000)
```

---

## 7. 项目代码框架（HAL 库，Keil MDK）

### 标准目录结构
```
<ProjectRoot>/
├── User/              ← 用户应用代码（main.c, xxx.c/h 放这里）
├── BSP/
│   ├── LCD/           ← lcd.c lcd.h  (FSMC LCD驱动, g_back_color全局)
│   ├── TOUCH/         ← touch.c touch.h (tp_dev结构体)
│   ├── LED/           ← led.c led.h
│   ├── KEY/           ← key.c key.h
│   ├── BEEP/          ← beep.c beep.h
│   ├── SPI/           ← spi.c spi.h (SPI1)
│   ├── NORFLASH/      ← norflash.c (W25Q128)
│   ├── SRAM/          ← sram.c (外部SRAM)
│   └── ...
├── SYSTEM/
│   ├── sys/           ← sys.h sys.c (时钟/RCC配置)
│   ├── delay/         ← delay.h delay.c (delay_ms, delay_us)
│   └── usart/         ← usart.h usart.c (USART1 printf重定向)
└── Middlewares/       ← FatFS / LwIP / FreeRTOS 等（按实验添加）
```

### main.c 标准初始化模板
```c
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/KEY/key.h"
#include "./BSP/TOUCH/touch.h"

int main(void)
{
    HAL_Init();                          /* 初始化 HAL 库 */
    sys_stm32_clock_init(336, 8, 2, 7); /* 设置时钟 168 MHz (HSE=8M, M=8, N=336, P=2, Q=7) */
    delay_init(168);                     /* 延时初始化 */
    usart_init(115200);                  /* USART1 初始化 (PA9/PA10 → CH340C) */
    led_init();                          /* PF9/PF10 输出推挽 */
    lcd_init();                          /* FSMC LCD 初始化 */
    key_init();                          /* PE2/PE3/PE4 下拉输入, PA0 上拉输入 */
    tp_dev.init();                       /* 触摸屏初始化 */
    /* 用户外设初始化... */
}
```

### LCD 关键全局变量
```c
extern uint16_t g_back_color;  /* 字符背景色，默认 WHITE(0xFFFF) */
/* ⚠️ 黑色背景页面必须在 lcd_clear() 前设置: g_back_color = 0x0000; */

extern _lcd_dev lcddev;  /* lcddev.width, lcddev.height, lcddev.id */
```

### 触摸屏结构体
```c
extern _tp_dev tp_dev;
/* 使用: tp_dev.scan(0);
         if (tp_dev.sta & TP_PRES_DOWN) { tx=tp_dev.x[0]; ty=tp_dev.y[0]; } */
```

### 按键定义
```c
/* key_scan(0) 返回值: */
#define KEY0_PRES  1   /* PE4 低有效 */
#define KEY1_PRES  2   /* PE3 低有效 */
#define KEY2_PRES  3   /* PE2 低有效 */
#define WKUP_PRES  4   /* PA0 高有效 */
```

### 新外设 GPIO 初始化模板（HAL）
```c
/* 示例：配置 PB6 为输出 */
__HAL_RCC_GPIOB_CLK_ENABLE();
GPIO_InitTypeDef gpio = {0};
gpio.Pin   = GPIO_PIN_6;
gpio.Mode  = GPIO_MODE_OUTPUT_PP;
gpio.Pull  = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOB, &gpio);
```

### USART3 扩展串口（最常用，PB10 TX / PB11 RX）
```c
/* 拔掉 P2 跳线帽后，直接 MCU-to-MCU 3.3V 连接 */
/* TX=PB10 AF7, RX=PB11 AF7 */
__HAL_RCC_GPIOB_CLK_ENABLE();
__HAL_RCC_USART3_CLK_ENABLE();
GPIO_InitTypeDef gpio = {0};
gpio.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
gpio.Mode      = GPIO_MODE_AF_PP;
gpio.Pull      = GPIO_PULLUP;
gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
gpio.Alternate = GPIO_AF7_USART3;
HAL_GPIO_Init(GPIOB, &gpio);
/* 注意: HAL_UART_RxCpltCallback 已被 usart.c 的 USART1 占用
         建议直接使用 USART3_IRQHandler + USART3->SR/DR 寄存器操作 */
```

---

## 8. 使用本 Skill 的注意事项

1. **引脚规划原则**：新外设优先选用第 5 节推荐的空闲 IO；若需占用条件可用引脚，先明确对应跳线帽设置。
2. **USART 冲突**：`usart.c` 中 `HAL_UART_RxCpltCallback` 已为 USART1 实现，新增 USART 请使用直接寄存器 ISR 或独立 Handle。
3. **LCD 背景色**：所有黑色背景页面必须在 `lcd_clear()` 前执行 `g_back_color = 0x0000`，否则文字周围出现白色矩形。
4. **SPI1 共享**：W25Q128 和 NRF24L01 共用 PB3/PB4/PB5，通过各自 CS（PB14 / PG7）分时复用，不可同时拉低 CS。
5. **FSMC 引脚只读**：所有 FSMC 相关引脚（约 40 个）在系统运行期间不可重配置为 GPIO。
