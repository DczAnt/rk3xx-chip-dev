# VPU JPEG 硬件编码参考

> VPU JPEG 硬编通过 ffmpeg-rockchip 的 `mjpeg_rkmpp` 编码器实现。
> **芯片能力差异**：RK3576/RK3588/RV1106 VPU 含 jpege 模块可用；**RK3566/3568 无 jpege，不可用**。
> 详见 `boards/<soc>.md` 的 "VPU JPEG" 章节。

## 一、可用性

| 芯片 | `mjpeg_rkmpp` 编码器 | 说明 |
|------|---------------------|------|
| RK3566 | ✗ | VPU 无 jpege 模块 |
| RK3568 | ✗ | VPU 无 jpege 模块 |
| RK3576 | ✓ | VPU 含 jpege |
| RK3588 | ✓ | VPU 含 jpege |
| RV1106 | ✓ | VPU 含 jpege |

> RK3566/3568 需用 CPU 软编 MJPEG（ffmpeg `mjpeg` 编码器 + `sws_scale`）。

---

## 二、使用方法（RK3576/RK3588/RV1106）

```c
// 1. 查找 mjpeg_rkmpp 编码器
const AVCodec *enc = avcodec_find_encoder_by_name("mjpeg_rkmpp");
if (!enc) { /* 芯片不支持或 ffmpeg 未编译 rkmpp */ }

// 2. 配置编码器
AVCodecContext *ctx = avcodec_alloc_context3(enc);
ctx->pix_fmt = AV_PIX_FMT_NV12;       // ⚠️ 必须 NV12，不接受 DRM_PRIME
ctx->width = 640;
ctx->height = 360;
ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);  // 复用硬解的 hw_device_ctx
ctx->qmin = 2;  ctx->qmax = 10;       // 质量
avcodec_open2(ctx, enc, NULL);

// 3. 编码流程: mmap DMA-BUF → memcpy NV12 → AVFrame → mjpeg_rkmpp → JPEG
// 从硬解输出的 DMA-BUF fd 取 NV12 数据
void *nv12 = mmap(NULL, buf_size, PROT_READ, MAP_SHARED, dma_fd, 0);
AVFrame *frame = av_frame_alloc();
frame->format = AV_PIX_FMT_NV12;
frame->width = 640;  frame->height = 360;
av_image_alloc(frame->data, frame->linesize, 640, 360, AV_PIX_FMT_NV12, 16);
memcpy(frame->data[0], nv12, 640 * 360 * 3 / 2);  // NV12 = w*h*1.5
munmap(nv12, buf_size);

avcodec_send_frame(ctx, frame);
while (avcodec_receive_packet(ctx, pkt) == 0) {
    // pkt->data: JPEG 数据
    av_packet_unref(pkt);
}
av_frame_free(&frame);
avcodec_free_context(&ctx);
```

---

## 三、关键限制

1. **不接受 `AV_PIX_FMT_DRM_PRIME`**: 报 "Unsupported input pixel format"，必须用 `AV_PIX_FMT_NV12` + 软件帧。
2. **必须 memcpy NV12**: 从 DMA-BUF mmap 后 memcpy 到 AVFrame，无法直接零拷贝喂 DRM_PRIME。
3. **VPU 仍执行硬件编码**: 虽然有 memcpy，但编码本身由 VPU 硬件完成，消除 CPU MJPEG 编码 + sws_scale 色彩转换。
4. **CPU 仅做小量 mmap+memcpy**: 约 345KB/帧（640×360 NV12），开销小。

---

## 四、收益对比

| 方案 | CPU 占用 | 编码耗时 | 说明 |
|------|---------|---------|------|
| CPU 软编 MJPEG | 高 | ~5-8ms/帧 | sws_scale + libjpeg |
| VPU 硬编 mjpeg_rkmpp | 低 | ~1-2ms/帧 | 仅 mmap+memcpy + VPU 硬件 |

> 适合需要大量 JPEG 快照的 AI 视频监控场景（每帧/每告警生成快照）。

---

## 五、替代方案（RK3566/3568 无 jpege）

```c
// CPU 软编 MJPEG (RK3566/3568)
const AVCodec *enc = avcodec_find_encoder_by_name("mjpeg");  // 软件编码器
ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
avcodec_open2(ctx, enc, NULL);
// 需 sws_scale 做 NV12→YUVJ420P 色彩转换
// 性能较差，但兼容所有芯片
```