# 开源模块案例

收录常用模块的开源驱动代码与参考工程链接。

---

## 电机驱动

| 模块 | 芯片 | 参考链接 |
|------|------|----------|
| L298N 电机驱动板 | L298N | [开源驱动](https://github.com/search?q=L298N+STM32) |
| TB6612FNG | TB6612 | [官方手册](https://toshiba.semicon-storage.com/info/TB6612FNG_datasheet_en_20141001.pdf) |
| A4950 直流电机驱动 | A4950 | B站搜索"A4950电机驱动" |
| DRV8833 | DRV8833 | [TI官方示例](https://www.ti.com/product/DRV8833) |

---

## 编码器

| 类型 | 说明 | 参考 |
|------|------|------|
| 霍尔编码器 | AB 相正交解码，测速精准 | B站搜索"STM32编码器测速" |
| 光电编码器 | 成本低，适合低精度场合 | - |

---

## IMU（惯性测量）

| 模块 | 芯片 | 参考链接 |
|------|------|----------|
| MPU6050 | MPU6050 | [江科大自化协教程](https://www.bilibili.com/video/BV1th411z7sn) |
| JY901 | - | 厂家提供例程（维特智能官网） |
| ICM42688 | ICM42688 | [开源库](https://github.com/bolderflight/icm42688) |

---

## 摄像头模块

| 模块 | 说明 | 参考链接 |
|------|------|----------|
| OV7725 | DVP 接口，适合 DSP 平台 | 逐飞科技开源例程 |
| OpenMV H7 | 内置 MicroPython，易上手 | [OpenMV 官方文档](https://docs.openmv.io/) |
| K210 MaixBit | AI 加速，适合目标检测 | [MaixPy 文档](https://maixpy.sipeed.com/) |
| 总钻风摄像头 | 配合逐飞 DSP 套件 | 逐飞科技官网 |

---

## 其他常用模块

| 模块 | 用途 | 备注 |
|------|------|------|
| HC-SR04 | 超声波测距 | 避障辅助 |
| TB6612 + 编码器 | 精确控速 | 推荐组合 |
| OLED（SSD1306） | 调试信息显示 | I2C/SPI |
| 蓝牙串口（HC-05） | 无线调参 | 简单易用 |

---

## 逐飞科技开源资料（强烈推荐）

- 官网：[https://www.seekfree.com](https://www.seekfree.com)
- 提供 TMS320F28379D、RT1064、CH32 等平台完整例程
- 包含：电机驱动、编码器、摄像头、灰度传感器等全套驱动
