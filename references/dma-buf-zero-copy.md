# DMA-BUF 全链路零拷贝管线

> RK3XX 生态核心优势：ffmpeg-rk 硬解 → RGA 缩放 → RKNN 推理，全程 DMA-BUF fd 传递，CPU 不参与像素搬运。
> 本文档是 RK 芯片专有能力的核心，依赖 ffmpeg-rockchip + RGA importbuffer_fd + RKNN set_io_mem。

## 一、零拷贝数据流

```
ffmpeg hevc_rkmpp 硬解 → AVFrame(DRM PRIME) → desc->objects[0].fd (DMA-BUF fd)
    ↓
RGA src = importbuffer_fd(mpp_fd)        // RGA 引用 MPP 的 DMA-BUF
RGA dst = importbuffer_fd(rknn_fd)       // RGA 引用 RKNN input mem
    ↓ improcess (缩放+格式转换, 硬件直接读 src 写 dst)
RKNN rknn_set_io_mem + rknn_run          // NPU 直接读 input mem
```

**关键**: 全程 DMA-BUF fd 传递，无 memcpy，CPU 不参与像素搬运。

---

## 二、ffmpeg C API 硬解（获取 DMA-BUF fd）

```cpp
// ffmpeg 头文件无 extern "C" 保护, C++ 中必须手动添加
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

// 1. 创建 DRM hwdevice (共享给多路解码)
AVBufferRef *hw_device_ctx = NULL;
av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_DRM, "/dev/dri/card0", NULL, 0);

// 2. 指定 hevc_rkmpp 硬件解码器
const AVCodec *codec = avcodec_find_decoder_by_name("hevc_rkmpp");
// 或 h264_rkmpp / av1_rkmpp / vp9_rkmpp 等

// 3. 配置解码器
AVCodecContext *dec_ctx = avcodec_alloc_context3(codec);
avcodec_parameters_to_context(dec_ctx, par);
dec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
dec_ctx->pix_fmt = AV_PIX_FMT_DRM_PRIME;  // 明确要求硬件输出
avcodec_open2(dec_ctx, codec, NULL);

// 4. 解码循环
avcodec_send_packet(dec_ctx, pkt);
while (avcodec_receive_frame(dec_ctx, frame) == 0) {
    // 提取 DRM PRIME fd (DMA-BUF fd, 零拷贝关键)
    AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0];
    int dma_fd = desc->objects[0].fd;            // DMA-BUF fd
    int buf_size = desc->objects[0].size;
    int pitch = desc->layers[0].planes[0].pitch; // hor_stride
    // dma_fd → RGA importbuffer_fd → RKNN (零拷贝)
    av_frame_unref(frame);
}
```

---

## 三、RGA + RKNN 零拷贝（RGA dst 直接引用 RKNN input mem）

```c
// 1. RKNN 创建 input_mem (零拷贝输入)
rknn_tensor_attr in_attr;
in_attr.index = 0;
rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &in_attr, sizeof(in_attr));
in_attr.type = RKNN_TENSOR_UINT8;
in_attr.fmt  = RKNN_TENSOR_NHWC;
rknn_tensor_mem *input_mem = rknn_create_mem(ctx, in_attr.size_with_stride);
rknn_set_io_mem(ctx, input_mem, &in_attr);

// 2. RGA dst 引用 RKNN input_mem 的 fd (零拷贝: RGA 写入 = NPU 输入)
rga_buffer_handle_t rga_dst_handle = importbuffer_fd(input_mem->fd, in_attr.size_with_stride);
rga_buffer_t rga_dst = wrapbuffer_handle(rga_dst_handle, 640, 640, RK_FORMAT_RGB_888);

// 3. RGA src 引用 MPP/ffmpeg 解码输出的 fd (零拷贝: 不拷贝 NV12)
rga_buffer_handle_t rga_src_handle = importbuffer_fd(mpp_fd, buf_size);
rga_buffer_t rga_src = wrapbuffer_handle_t(rga_src_handle, w, h, hs, vs, RK_FORMAT_YCbCr_420_SP);

// 4. RGA 执行缩放+格式转换 (硬件直接读 mpp_fd 写 rknn_fd)
im_rect srect = {0, 0, w, h};
im_rect drect = {pad_x, pad_y, dw, dh};  // letterbox
imfill(rga_dst, dst_full, 0x727272, IM_SYNC);  // 填充灰边
IM_STATUS rs = improcess(rga_src, rga_dst, {}, srect, drect, {0,0,0,0}, -1, NULL, NULL, IM_SYNC);
if (rs != IM_STATUS_SUCCESS) printf("RGA fail %d\n", rs);  // 注意: SUCCESS=1
releasebuffer_handle(rga_src_handle);

// 5. RKNN 推理 (NPU 直接读 input_mem, 无需 rknn_inputs_set)
rknn_run(ctx, NULL);

// 6. 读取输出 (零拷贝, 直接读 out_mems[i]->virt_addr)
int8_t *out = (int8_t*)out_mems[i]->virt_addr;
```

---

## 四、多路并发架构

```
[解码线程×N] RTSP → hevc_rkmpp 硬解 → DRM PRIME fd → FrameQueue(最新帧)
[推理线程×M] 轮询 N 个 FrameQueue → RGA 零拷贝 → RKNN 零拷贝 → 后处理
```

### 关键设计

```cpp
#define SKIP_INF 4   // 跳帧: 每路 20fps 解码 / 4 = 5fps 推理
#define MAX_CH 4     // 通道数, 按芯片 NPU 算力调整

// 每路帧队列: 最新帧策略(满时丢弃旧帧)
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int fd;          // DRM PRIME fd (DMA-BUF)
    int w, h, hs, vs, buf_size;
    int frame_idx;
    int ready;
} FrameQueue;

// 解码线程: 跳帧后送 FrameQueue
if (dec_count % SKIP_INF == 0) {
    pthread_mutex_lock(&q->mutex);
    q->fd = drm_fd;  // 最新帧覆盖旧帧
    q->ready = 1;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

// 推理线程: 轮询 N 路, NPU 串行处理
for (int ch = 0; ch < n_ch; ch++) {
    if (!queues[ch]->ready) continue;
    // RGA 零拷贝 + RKNN 零拷贝 (同 §三)
    rknn_run(rknn_ctx, NULL);
}
```

### 多核 NPU 并行（RK3576/RK3588）

- 单核（RK3566/3568）: 1 个 RKNN context 串行处理 N 路
- 双核（RK3576）: 2 个 context + 2 个推理线程，各处理一半通道
- 三核（RK3588）: 3 个 context + 3 个推理线程

---

## 五、命令行限制（重要）

| 限制 | 说明 | 替代方案 |
|------|------|---------|
| `hwdownload` 滤镜 | 板上报 "Function not implemented" | 用 C API |
| `scale_rkrga` 命令行 | 格式协商失败 "Impossible to convert between formats" | 用 C API |
| DRM PRIME 导出 | 命令行无法将 DRM PRIME 帧导出给外部程序 | 用 C API |

> **结论**: 命令行方式无法实现零拷贝管线，**只有 C API 方式可行**。

---

## 六、静态库链接（板上直接编译）

```bash
# 静态库直接指定路径, 不需要 -l/-L
g++ -o pipeline pipeline.cpp \
    -I/opt/ffmpeg-build \
    -I/usr/include/rga -I/usr/include/rknn -I/usr/include/rockchip \
    /opt/ffmpeg-build/libavformat/libavformat.a \
    /opt/ffmpeg-build/libavcodec/libavcodec.a \
    /opt/ffmpeg-build/libavfilter/libavfilter.a \
    /opt/ffmpeg-build/libswresample/libswresample.a \
    /opt/ffmpeg-build/libswscale/libswscale.a \
    /opt/ffmpeg-build/libavutil/libavutil.a \
    -lrockchip_mpp -lrga -lrknnrt \
    -lpthread -lm -lz -ldl -ldrm
```

> 路径因芯片而异，见 `boards/<soc>.md` 的 "SDK 路径" 章节。

---

## 七、关键注意事项

1. **ffmpeg extern "C" 保护**: C++ 中必须手动添加，否则链接失败
2. **`AV_PIX_FMT_DRM_PRIME`**: 明确要求硬件输出，才能拿到 DMA-BUF fd
3. **DRM PRIME fd 提取**: `AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0]; int fd = desc->objects[0].fd;`
4. **RGA wrapbuffer_handle_t 6 参数版**: 跨设备 NV12 必须传入实际 stride
5. **RKNN set_io_mem**: NPU 直接读 input_mem，无需 rknn_inputs_set
6. **IM_STATUS_SUCCESS = 1**: 判断 RGA 成功用 `rs == IM_STATUS_SUCCESS`
7. **静态库直接指定路径**: 不需要 `-l` 和 `-L`