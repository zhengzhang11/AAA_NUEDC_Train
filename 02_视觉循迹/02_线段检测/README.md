# 线段检测

从二值化图像中提取轨道线位置信息，输出横向偏差量。

## 主流算法对比

| 算法 | 优点 | 缺点 |
|------|------|------|
| 逐行扫线（质心） | 简单快速 | 噪声敏感 |
| 霍夫变换 | 鲁棒性强 | 计算量较大 |
| 边缘拟合（最小二乘） | 精度高 | 实现稍复杂 |

## 推荐：逐行扫线 + 质心加权

```c
// 伪代码示意
for each ROI row:
    find left_edge and right_edge
    center = (left_edge + right_edge) / 2
weighted_center = weighted_avg(centers, row_weights)
error = weighted_center - IMAGE_WIDTH / 2
```

## 输出

- `error`：轨道中心相对图像中心的横向偏差（像素）

## 文件说明

- `src/line_detect.c` — 扫线与偏差计算
