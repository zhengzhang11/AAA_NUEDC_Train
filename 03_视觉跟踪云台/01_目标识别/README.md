# 目标识别

通过摄像头识别目标（颜色块/特定形状），输出目标在图像中的坐标。

## 识别方式

### 颜色识别（推荐入门）

```python
# OpenMV 示例
img = sensor.snapshot()
blobs = img.find_blobs([COLOR_THRESHOLD], pixels_threshold=200)
if blobs:
    b = max(blobs, key=lambda b: b.pixels())
    cx, cy = b.cx(), b.cy()
```

### 形状识别

- 圆形：`find_circles()`
- 矩形：`find_rects()`

## 输出格式

| 字段 | 说明 |
|------|------|
| `cx` | 目标中心 X 坐标（像素） |
| `cy` | 目标中心 Y 坐标（像素） |
| `found` | 是否检测到目标（0/1） |

## 注意事项

- 颜色阈值需在实际光线下标定（使用 OpenMV IDE 阈值编辑器）
- 增加像素面积过滤，排除噪点干扰
- 目标丢失时云台应保持原位或缓慢扫描

## 文件说明

- `src/detect.py` — OpenMV 目标识别脚本
