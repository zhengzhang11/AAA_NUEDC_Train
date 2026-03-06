# 控制算法

将图像偏差量转换为电机差速控制输出。

## PID 控制

```
error = 图像横向偏差（像素）
PID 输出 → 左右电机转速差 → 差速转向
```

## 速度分级策略

| 场景 | 速度 | 说明 |
|------|------|------|
| 直道 | 高速 | `|error| < threshold_low` |
| 缓弯 | 中速 | `threshold_low ≤ |error| < threshold_high` |
| 急弯 | 低速 | `|error| ≥ threshold_high` |

## 参数整定顺序

1. 固定低速，整定 `Kp`，消除静差
2. 增大速度，加入 `Kd`，抑制震荡
3. 长距离测试，微调 `Ki`

## 文件说明

- `src/pid_control.c` — PID 控制器实现
- `src/motor_driver.c` — 电机 PWM 输出
