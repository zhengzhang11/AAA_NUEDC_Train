# 通信接口

视觉模块（OpenMV / K210）与主控（STM32 / DSP）之间的数据通信协议。

## 串口通信协议（推荐）

| 字段 | 字节数 | 类型 | 说明 |
|------|--------|------|------|
| 帧头 | 2B | `0xAA 0xBB` | 固定帧头 |
| found | 1B | uint8 | 1=检测到目标，0=未检测到 |
| cx | 2B | int16 | 目标中心 X（像素） |
| cy | 2B | int16 | 目标中心 Y（像素） |
| 校验 | 1B | XOR | 数据字节异或校验 |
| 帧尾 | 1B | `0xCC` | 固定帧尾 |

## 波特率建议

- `115200` bps（推荐）或 `460800` bps（高帧率需求）

## OpenMV 发送示例

```python
import struct, uart

uart = UART(3, 115200)

def send_data(found, cx, cy):
    checksum = found ^ (cx & 0xFF) ^ ((cx >> 8) & 0xFF) ^ \
               (cy & 0xFF) ^ ((cy >> 8) & 0xFF)
    buf = struct.pack('<BBhhhB', 0xAA, 0xBB, found, cx, cy, checksum)
    uart.write(buf + b'\xCC')
```

## 文件说明

- `src/uart_protocol.c` — 主控串口解析
- `src/send_data.py` — OpenMV 发送脚本
