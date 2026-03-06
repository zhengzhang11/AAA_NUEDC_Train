# 云台控制

双轴（Yaw 水平 / Pitch 俯仰）独立 PID 控制，驱动舵机跟踪目标。

## 舵机 PWM 规格

| 参数 | 值 |
|------|-----|
| 频率 | 50 Hz |
| 最小脉宽 | 500 μs（0°） |
| 中位脉宽 | 1500 μs（90°） |
| 最大脉宽 | 2500 μs（180°） |

## PID 控制逻辑

```
ex = IMAGE_WIDTH/2  - cx    # 水平偏差
ey = IMAGE_HEIGHT/2 - cy    # 垂直偏差

yaw_output   = PID_yaw(ex)
pitch_output = PID_pitch(ey)

servo_yaw   += yaw_output
servo_pitch += pitch_output
```

## 限幅保护

- 舵机角度限制在机械范围内，避免堵转损坏
- 输出增量限幅，防止突变

## 调参建议

- 先单轴调试（固定另一轴）
- 以目标不震荡为前提逐步增大 Kp
- 适当加入 Kd 改善超调

## 文件说明

- `src/servo_ctrl.c` — 舵机 PWM 控制
- `src/gimbal_pid.c` — 双轴 PID 控制器
