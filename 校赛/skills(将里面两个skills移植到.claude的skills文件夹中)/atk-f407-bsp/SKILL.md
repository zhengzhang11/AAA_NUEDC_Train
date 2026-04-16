---
name: atk-f407-bsp
description: >
  ATK Explorer STM32F407 V3 BSP code borrowing workflow.
  When the current project needs a driver or peripheral that is not yet
  implemented, search the 60-experiment reference library, read the most
  relevant BSP source, adapt it to the current project (pin map, includes,
  Keil groups), or generate fresh code if no good match exists.
---

# ATK-F407 BSP Code Borrowing Skill

## Purpose

When the current project (实验28 触摸屏实验 or any other working project under
the same SDK) lacks a BSP driver, peripheral config, or middleware, follow this
3-phase workflow:

1. **SEARCH** — keyword-match the need against the experiment index below
2. **READ** — navigate into the matched experiment, read its BSP source files
3. **ADAPT or GENERATE** — integrate adapted code, or generate from scratch if
   no good match is found

---

## Phase 0 – Decide whether search is needed

Before searching the library, check the current project:

```
Drivers/BSP/<module>/    ← already exists?  If yes, skip search.
User/<feature>.c/.h      ← already custom-coded?  If yes, skip search.
```

Only invoke the library search when the driver/feature is genuinely missing.

---

## Phase 1 – Keyword → Experiment mapping

### Base path of the experiment library

```
E:\2026电赛\【正点原子】探索者STM32F407开发板V3 资料盘(A盘)\2，标准例程-HAL库版本\2，标准例程-HAL库版本\
```

All 60 experiments live directly under this base path.  Use Glob / Bash `ls`
to list them when needed.

### Full experiment index (keyword → folder name)

| # | Folder name | Key peripherals / keywords |
|---|-------------|---------------------------|
| 0 | 实验0 跑马灯实验 | LED, GPIO output, PA8 PE6 |
| 0a | 实验0 跑马灯实验\实验0-1 C语言跑马灯实验 | LED blink, HAL_GPIO_TogglePin |
| 0b | 实验0 跑马灯实验\实验0-2 寄存器版本跑马灯实验 | LED register, RCC GPIOA GPIOE |
| 0c | 实验0 跑马灯实验\实验0-3 HAL库版本跑马灯实验 | LED HAL, BSP LED driver |
| 0d | 实验0 跑马灯实验\实验0-4 HAL库固件库版本跑马灯实验 | LED firmware |
| 1 | 实验1 按键输入实验 | KEY, GPIO input, PA0 PE2 PE3 PE4, debounce |
| 2 | 实验2 串口实验 | USART, UART, printf, baud rate, HAL_UART |
| 3 | 实验3 外部中断实验 | EXTI, interrupt, NVIC, KEY EXTI |
| 4 | 实验4 独立看门狗实验 | IWDG, watchdog, HAL_IWDG |
| 5 | 实验5 窗口看门狗实验 | WWDG, window watchdog |
| 6 | 实验6 定时器中断实验 | TIM, timer, interrupt, HAL_TIM |
| 7 | 实验7 PWM输出实验 | PWM, TIM PWM, HAL_TIM_PWM, duty cycle |
| 8 | 实验8 定时器输入捕获实验 | capture, input capture, TIM IC |
| 8a | 实验8 定时器输入捕获实验\实验8-1 定时器输入捕获实验 | TIM capture |
| 8b | 实验8 定时器输入捕获实验\实验8-2 定时器输入捕获实验（高速）| high-speed capture |
| 9 | 实验9 电容触摸按键实验 | capacitive touch key, TPAD, RC charge |
| 9a | 实验9 电容触摸按键实验\实验9-1 定时器HAL库电容触摸按键实验 | TPAD HAL |
| 9b | 实验9 电容触摸按键实验\实验9-2 定时器寄存器电容触摸按键实验 | TPAD register |
| 10 | 实验10 OLED显示实验 | OLED, SSD1306, I2C OLED, SPI OLED |
| 10a | 实验10 OLED显示实验\实验10-1 0.96寸OLED显示实验 | 0.96 OLED |
| 10b | 实验10 OLED显示实验\实验10-2 2.42寸OLED显示实验 | 2.42 OLED |
| 11 | 实验11 TFTLCD显示实验 | TFTLCD, FSMC, LCD, ILI9341, NT35310 |
| 12 | 实验12 USMART调试实验 | USMART, debug, command parser |
| 13 | 实验13 RTC实时时钟实验 | RTC, real-time clock, calendar, HAL_RTC |
| 14 | 实验14 待机唤醒实验 | standby, wakeup, low power, PWR |
| 15 | 实验15 ADC实验 | ADC, analog, HAL_ADC, DMA ADC |
| 16 | 实验16 DAC实验 | DAC, analog output, HAL_DAC, PA4 PA5 |
| 17 | 实验17 DMA实验 | DMA, DMA transfer, HAL_DMA |
| 17a | 实验17 DMA实验\实验17-1 DMA实验（存储器到存储器）| DMA M2M |
| 17b | 实验17 DMA实验\实验17-2 DMA实验（存储器到外设）| DMA M2P USART |
| 18 | 实验18 IIC实验 | I2C, IIC, software I2C, HAL_I2C, AT24C02 EEPROM |
| 19 | 实验19 SPI实验 | SPI, HAL_SPI, W25Q128, NOR Flash |
| 19a | 实验19 SPI实验\实验19-1 SPI实验 | SPI W25Q128 |
| 19b | 实验19 SPI实验\实验19-2 SPI DMA实验 | SPI DMA |
| 20 | 实验20 485通信实验 | RS485, USART2, half-duplex |
| 21 | 实验21 CAN通信实验 | CAN, CAN bus, HAL_CAN |
| 22 | 实验22 触摸屏实验 | touch, touchscreen, resistive, capacitive, GT9147, FT5206 |
| 22a | 实验22 触摸屏实验\实验22-1 电阻触摸屏 | resistive touch, XPT2046 |
| 22b | 实验22 触摸屏实验\实验22-2 电容触摸屏 | capacitive touch, GT9147 |
| 23 | 实验23 红外遥控实验 | IR, infrared, remote control, NEC protocol |
| 24 | 实验24 DS18B20温度传感器实验 | DS18B20, 1-wire, temperature sensor |
| 25 | 实验25 DHT11温湿度传感器实验 | DHT11, humidity, temperature |
| 26 | 实验26 无线通信实验 | NRF24L01, wireless, 2.4GHz, SPI RF |
| 27 | 实验27 FLASH模拟EEPROM实验 | Flash EEPROM, STM32 internal Flash, HAL_FLASH |
| 28 | 实验28 摄像头实验 | OV7670, camera, DCMI, image capture |
| 29 | 实验29 内存管理实验 | memory, malloc, mymalloc, heap |
| 30 | 实验30 SD卡实验 | SD card, SDIO, HAL_SD, FAT |
| 31 | 实验31 FATFS实验 | FATFS, file system, f_open, f_write |
| 32 | 实验32 汉字显示实验 | Chinese font, GBK, font library, Flash font |
| 33 | 实验33 图片显示实验 | image, BMP, JPEG, picture display |
| 34 | 实验34 照相机实验 | camera, OV2640, photo |
| 35 | 实验35 音乐播放器实验 | audio, WAV, I2S, WM8978, MP3 |
| 36 | 实验36 录音机实验 | recording, microphone, I2S WM8978 |
| 37 | 实验37 视频播放器实验 | video, AVI, player |
| 38 | 实验38 综合测试实验 | comprehensive test |
| 39 | 实验39 SRAM实验 | SRAM, FSMC SRAM, IS62WV51216 external RAM |
| 40 | 实验40 内存管理（扩展SRAM）实验 | SRAM memory management |
| 41 | 实验41 USB读卡器实验 | USB, USB MSC, card reader |
| 42 | 实验42 USB虚拟串口实验 | USB CDC, virtual COM port, VCP |
| 43 | 实验43 网络通信实验 | Ethernet, LAN8720, LWIP, TCP UDP |
| 44 | 实验44 手机热点通信实验 | WiFi, hotspot, socket |
| 45 | 实验45 FreeRTOS移植实验 | FreeRTOS, RTOS, task scheduler |
| 46 | 实验46 FreeRTOS任务管理实验 | FreeRTOS tasks, vTaskCreate, priority |
| 46a | 实验46 FreeRTOS任务管理实验\实验46-1 FreeRTOS任务挂起和恢复实验 | suspend resume |
| 46b | 实验46 FreeRTOS任务管理实验\实验46-2 FreeRTOS任务删除实验 | delete task |
| 47 | 实验47 FreeRTOS时间管理实验 | FreeRTOS tick, vTaskDelay |
| 47a | 实验47 FreeRTOS时间管理实验\实验47-1 FreeRTOS相对延时实验 | vTaskDelay relative |
| 47b | 实验47 FreeRTOS时间管理实验\实验47-2 FreeRTOS绝对延时实验 | vTaskDelayUntil |
| 48 | 实验48 FreeRTOS消息队列实验 | FreeRTOS queue, xQueueCreate |
| 49 | 实验49 FreeRTOS信号量实验 | FreeRTOS semaphore, xSemaphore |
| 50 | 实验50 FreeRTOS互斥量实验 | FreeRTOS mutex, xMutex |
| 50a | 实验50 FreeRTOS互斥量实验\实验50-1 FreeRTOS互斥量实验 | mutex basic |
| 50b | 实验50 FreeRTOS互斥量实验\实验50-2 FreeRTOS优先级翻转实验 | priority inversion |
| 51 | 实验51 FreeRTOS软件定时器实验 | FreeRTOS timer, xTimerCreate |
| 52 | 实验52 FreeRTOS事件标志组实验 | FreeRTOS event group, xEventGroupCreate |
| 53 | 实验53 FreeRTOS任务通知实验 | FreeRTOS task notify, xTaskNotify |
| 54 | 实验54 FreeRTOS低功耗实验 | FreeRTOS tickless, low power |
| 55 | 实验55 FreeRTOS内存管理实验 | FreeRTOS heap, pvPortMalloc |
| 56 | 实验56 DSP BasicMath测试实验 | DSP, CMSIS DSP, ARM math |
| 57 | 实验57 DSP FFT测试实验 | DSP FFT, arm_rfft, spectrum |
| 58 | 实验58 DSP滤波器实验 | DSP filter, FIR IIR, arm_fir |
| 59 | 实验59 综合实验 | comprehensive demo |

> **Note on sub-experiments:** folders with `\实验N-M` sub-folders contain
> multiple variants.  When searching, check the parent folder first, then
> descend into the most relevant sub-folder.

---

## Phase 2 – Reading BSP source files

### Standard directory layout inside each experiment

```
实验N XXXXX/
├── Drivers/
│   ├── BSP/               ← primary target: one sub-folder per peripheral
│   │   ├── LCD/           ← lcd.h, lcd.c, lcdfont.h
│   │   ├── KEY/           ← key.h, key.c
│   │   ├── LED/           ← led.h, led.c
│   │   ├── TOUCH/         ← touch.h, touch.c, ctiic.h, ctiic.c …
│   │   ├── SRAM/          ← sram.h, sram.c
│   │   ├── SPI/           ← spi.h, spi.c
│   │   ├── IIC/           ← myiic.h, myiic.c
│   │   ├── RS485/         ← rs485.h, rs485.c
│   │   ├── CAN/           ← can.h, can.c
│   │   ├── W25QXX/        ← w25qxx.h, w25qxx.c
│   │   ├── NORFLASH/      ← norflash.h, norflash.c
│   │   ├── SDIO/          ← sdio_sdcard.h, sdio_sdcard.c
│   │   ├── ES8388/        ← es8388.h, es8388.c (audio codec)
│   │   ├── OV7670/        ← ov7670.h, ov7670.c
│   │   └── …
│   ├── CMSIS/             ← ARM core headers (no modification needed)
│   └── STM32F4xx_HAL_Driver/ ← HAL library (no modification needed)
├── Middlewares/           ← FATFS, FreeRTOS, LWIP, USB if present
├── User/
│   ├── main.c
│   ├── usart.c / usart.h  ← USART1 printf redirect
│   ├── adc.c  / adc.h     ← if ADC used
│   └── …
├── Projects/              ← Keil .uvprojx
└── readme.txt             ← always read this first for pin / config notes
```

### Reading order

1. `readme.txt` — confirms the peripheral's function and any board-specific notes
2. `Drivers/BSP/<module>/<module>.h` — macro defines, struct types, API prototypes
3. `Drivers/BSP/<module>/<module>.c` — initialization, IRQ handlers, core logic
4. `User/main.c` lines near `/* USER CODE BEGIN 2 */` — how the module is
   initialized and called

---

## Phase 3 – Adapt or Generate

### When to ADAPT from the reference experiment

Adapt when ALL of these are true:
- A clear keyword match exists in the index (Phase 1)
- The BSP folder for the module is found in `Drivers/BSP/`
- The peripheral's core logic is portable (not tightly coupled to a
  specific demo task that conflicts with the current project)

**Adaptation checklist:**

| Step | Action |
|------|--------|
| Pins | Cross-reference with `atk-f407` skill pin tables; remap any macros that differ |
| Clock | Verify `__HAL_RCC_<PERIPH>_CLK_ENABLE()` calls match the target peripheral instance |
| Includes | Change `"../../Drivers/BSP/…"` relative paths to match the current project's tree |
| Keil groups | Add new `.c` files to the correct Keil project group (BSP group preferred) |
| IRQ priority | Confirm NVIC priority does not conflict with existing USART1/TOUCH/TIM handlers |
| Global state | Rename any globals that conflict with existing names in the current project |
| HAL callback | If the reference uses `HAL_UART_RxCpltCallback`, check for collision with `usart.c` |

### When to GENERATE fresh code

Generate when ANY of these is true:
- No experiment in the index matches the needed peripheral
- The matched experiment's BSP code is deeply coupled to its own demo (e.g.
  photo capture demo calling SD card + FATFS simultaneously)
- Adapting would require changing more than ~40 % of the source lines
- The user explicitly prefers fresh code

**Generation standards:**

```c
/* Template for a new BSP driver file */

/**
 * @file  bsp_<module>.c
 * @brief <Module> driver for ATK Explorer STM32F407 V3.
 *        HAL library.  All comments ASCII (Keil GBK-safe).
 */

#include "bsp_<module>.h"

/* ---- private macros ---------------------------------------------------- */
/* ---- private types ----------------------------------------------------- */
/* ---- private variables ------------------------------------------------- */
/* ---- private function prototypes --------------------------------------- */

/* ======================================================================== */
/* Public API                                                                */
/* ======================================================================== */

/**
 * @brief Initialize <module>.
 * @retval 0 OK, 1 error
 */
uint8_t bsp_<module>_init(void)
{
    /* clock enable */
    /* GPIO init */
    /* peripheral init */
    return 0u;
}
```

Rules for generated code:
- Pure ASCII comments (no Chinese in .c/.h to stay Keil GBK-safe)
- HAL-based (not bare register) unless performance demands it
- Defensive `uint8_t` return values (0=OK, 1=error)
- No dynamic allocation — use static buffers
- No `printf` inside BSP drivers — return status codes; let the application print

---

## Integration checklist (both adapt and generate)

```
[ ] Header .h placed in Drivers/BSP/<MODULE>/
[ ] Source .c placed in Drivers/BSP/<MODULE>/
[ ] Include path added in Keil: Options → C/C++ → Include Paths
[ ] Source file added to Keil project tree (right-click group → Add existing)
[ ] Corresponding #include added in the calling .c file
[ ] Pin conflicts checked against atk-f407 skill conflict table
[ ] IRQ priorities set (lower number = higher priority; do not clash with TIM6 used by delay)
[ ] Tested with a minimal call in main() before wiring into the full application
```

---

## Quick decision flowchart

```
Need peripheral X in current project?
│
├─ Already in Drivers/BSP/ or User/?  → Use it as-is.
│
└─ Missing
   │
   ├─ Keyword match in index?
   │   ├─ YES → Read readme.txt + BSP source
   │   │         ├─ Easily portable (< 40% changes)?  → ADAPT
   │   │         └─ Deeply coupled to demo?           → GENERATE
   │   │
   │   └─ NO  → GENERATE fresh code directly
   │
   └─ After adapt/generate: run Integration checklist
```

---

## Common peripheral keyword shortcuts

| You say… | Search keyword | Likely experiment |
|-----------|---------------|-------------------|
| encoder / pulse count | capture, TIM IC | 实验8 |
| motor PWM | PWM, TIM PWM | 实验7 |
| EEPROM / persistent storage | IIC, AT24C02 | 实验18 |
| SPI Flash / W25Q | SPI, W25Q128 | 实验19 |
| external SRAM | SRAM, FSMC SRAM | 实验39 |
| ADC analog read | ADC | 实验15 |
| DAC analog output | DAC | 实验16 |
| CAN bus | CAN | 实验21 |
| USB serial | USB CDC, VCP | 实验42 |
| FreeRTOS | RTOS, FreeRTOS | 实验45–55 |
| DS18B20 temp | 1-wire, DS18B20 | 实验24 |
| DHT11 temp+humi | DHT11 | 实验25 |
| SD card | SDIO, SD card | 实验30 |
| FAT file system | FATFS | 实验31 |
| Chinese font | GBK, font library | 实验32 |
| FFT / DSP | DSP FFT, CMSIS DSP | 实验57 |
| wireless | NRF24L01, 2.4GHz | 实验26 |
| Ethernet / TCP | LAN8720, LWIP | 实验43 |

---

## Notes on the current working project (实验28 触摸屏实验)

- This project already has: LCD, TOUCH (resistive + capacitive), KEY, LED,
  USART1 (printf), USART3 (motor data), TIM (delay), SRAM (if needed)
- Do NOT copy the entire BSP from another experiment — copy only the
  specific driver sub-folder needed
- `usart.c` owns `HAL_UART_RxCpltCallback` for USART1.  Any new UART
  peripherals must use direct ISR (`USARTx_IRQHandler`) with raw `SR/DR`
  register access to avoid HAL callback conflicts
- The project uses GBK source encoding in Keil.  All new .c/.h files must
  be ASCII-only comments (or explicit GBK byte arrays as done in
  `chinese_font.c`)
