# RGA 2D 硬件加速 API 参考

> RGA (Rockchip Graphics Acceleration) 2D 引擎，用于图像缩放、旋转、格式转换、裁剪。
> 芯片 RGA 实例数见 `boards/<soc>.md`（RK3566/3568 单实例，RK3576 双实例，RK3588 三实例）。

## 一、基本用法 (IM2D API)

```c
#include "im2d.h"
#include "RgaUtils.h"

// 1. 包装源/目标 buffer (从 fd 或虚拟地址)
rga_buffer_t src = wrapbuffer_fd(fd_nv12, width, height, FORMAT_NV12);
rga_buffer_t dst = wrapbuffer_fd(fd_rgb, 640, 640, FORMAT_RGB888);
// 或从虚拟地址: wrapbuffer_virtual(ptr, w, h, format)

// 2. 执行 RGA 操作: 缩放 + 格式转换
IM_STATUS status = improcess(src, dst, NULL, 0, 0, 0, 0, 0, 0, 0);
// 注意: IM_STATUS_SUCCESS = 1, 不是 0

// 3. 旋转 (90°/180°/270°)
improcess(src, dst, NULL, 0, 0, 0, 0, 0, 0, IM_HAL_TRANSFORM_ROT_90);
```

---

## 二、零拷贝用法 (importbuffer_fd，推荐)

```c
// 从 DMA-BUF fd 导入 (不拷贝, RGA 直接引用该 fd)
rga_buffer_handle_t handle = importbuffer_fd(dma_fd, buf_size);
rga_buffer_t rga_buf = wrapbuffer_handle_t(handle, w, h, hor_stride, ver_stride, RK_FORMAT_YCbCr_420_SP);
// ⚠️ 跨设备(NV12 from MPP)时必须用 6 参数版本传入实际 stride
// ... improcess ...
releasebuffer_handle(handle);
```

---

## 三、双实例/三实例调度（RK3576/RK3588）

```c
#include "im2d_type.h"  // IM_SCHEDULER_RGA2_CORE0 等

// im_opt_t 指定 RGA scheduler core
im_opt_t rga_opt = {};
rga_opt.core = IM_SCHEDULER_RGA2_CORE0;  // = 1<<2 = 4 → scheduler[0]
// 或 IM_SCHEDULER_RGA2_CORE1 = 1<<3 = 8 → scheduler[1]

// C++ 版 improcess (带 im_opt_t* 参数, 第 9 个参数)
improcess(src, dst, {}, srect, drect, {0,0,0,0}, -1, NULL, &rga_opt, IM_SYNC);

// ⚠️ C 版 improcess 无 im_opt_t 参数, 必须用 C++ 10 参数版
// ⚠️ RK3566/3568 单实例, im_opt_t 无意义
```

### 枚举值

| 枚举 | 值 | 说明 |
|------|-----|------|
| IM_SCHEDULER_RGA2_CORE0 | 4 (1<<2) | scheduler[0] |
| IM_SCHEDULER_RGA2_CORE1 | 8 (1<<3) | scheduler[1] |

---

## 四、关键陷阱（实测验证）

1. **`imsetColorSpace` 会导致 `improcess` 失败**: 不要调用 `imsetColorSpace`，RGA 默认色彩转换可用。
2. **`IM_STATUS_SUCCESS` = 1, 不是 0**: 判断成功用 `rs == IM_STATUS_SUCCESS`，不是 `rs == 0`。
3. **`wrapbuffer_handle_t` 需传入正确 stride**:
   - 4 参数版 `wrapbuffer_handle(h, w, fmt)` 自动算 stride，**同设备**内存可用
   - 跨设备（NV12 from MPP/ffmpeg）时**必须用 6 参数版** `wrapbuffer_handle_t(h, w, h_stride, v_stride, fmt)` 传入实际 stride，否则出错
4. **输入输出 stride 需 16 字节对齐**。
5. **支持 NV12→RGB 转换**（解码后转 RGB 给 RKNN）。
6. **有静态库 librga.a 可静态链接**，混合动态链接时需板子 crt 方案（见 `knowledge/cross-compile-glibc.md`）。
7. **imfill 无 im_opt_t 版本**: 保持默认 core 即可。
8. **双实例需 C++ 版 improcess**: C 版无 `im_opt_t` 参数。

---

## 五、典型用法

### NV12 → RGB 缩放（解码后给 RKNN）

```c
// MPP 解码输出 NV12, RGA 转 RGB 并缩放到模型输入尺寸
rga_buffer_handle_t src_h = importbuffer_fd(mpp_fd, mpp_buf_size);
rga_buffer_t src = wrapbuffer_handle_t(src_h, w, h, hs, vs, RK_FORMAT_YCbCr_420_SP);

rga_buffer_handle_t dst_h = importbuffer_fd(rknn_input_mem->fd, rknn_size);
rga_buffer_t dst = wrapbuffer_handle(dst_h, 640, 640, RK_FORMAT_RGB_888);

im_rect srect = {0, 0, w, h};
im_rect drect = {pad_x, pad_y, dw, dh};  // letterbox
imfill(dst, dst_full, 0x727272, IM_SYNC);  // 填充灰边
IM_STATUS rs = improcess(src, dst, {}, srect, drect, {0,0,0,0}, -1, NULL, NULL, IM_SYNC);
if (rs != IM_STATUS_SUCCESS) printf("RGA fail %d\n", rs);
releasebuffer_handle(src_h);
```

### 多路合成到单 plane（DRM 显示）

```c
// 4 路各缩放到 960×540，拼接为 2×2 网格 (1920×1080)
// 格式转换: NV12 → XRGB8888 (RGA 硬件加速)
// 合成目标用 wrapbuffer_handle_t (6参数版本，传入实际 stride)
// → DRM dumb buffer SetPlane 显示
```

---

## 六、链接方式

| 场景 | 链接方式 |
|------|---------|
| 仅 RGA | `-lrga` 静态（librga.a 存在）或动态 |
| RGA + RKNN/MPP 混合 | 必须动态链接 + 板子 crt 方案（见 `knowledge/cross-compile-glibc.md`） |