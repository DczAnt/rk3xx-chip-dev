# MPP 媒体处理平台 API 参考

> MPP (Media Process Platform) 是 Rockchip 通用媒体处理软件平台，屏蔽芯片底层差异，提供统一 MPI 接口。
> 本文档覆盖 RK3XX 系列通用 MPP 用法，芯片特定能力（VDPU/VEPU 型号、JPEG 硬编）见 `boards/<soc>.md`。

## 一、功能支持

- **视频解码**: H.265 / H.264 / H.263 / AV1 / VP9 / VP8 / MPEG-4 / MPEG-2 / AVS2 / AVS / MJPEG
- **视频编码**: H.265 / H.264 / VP8 / MJPEG
- **视频处理**: 拷贝、缩放、色彩空间转换、去交织

## 二、MPI 核心接口 (rk_mpi.h)

```c
MPP_RET mpp_create(MppCtx *ctx, MppApi **mpi);
MPP_RET mpp_init(MppCtx ctx, MppCtxType type, MppCodingType coding);
MPP_RET mpp_destroy(MppCtx ctx);
```

### 2.1 MppApi 函数指针

| 函数指针 | 说明 |
|---------|------|
| `decode_put_packet` | 异步解码-输入码流 |
| `decode_get_frame` | 异步解码-获取图像 |
| `encode_put_frame` | 异步编码-输入图像 |
| `encode_get_packet` | 异步编码-获取码流 |
| `control` | 控制接口（配置/查询） |
| `reset` | 复位 MPP 实例 |

### 2.2 核心类型

```c
typedef enum { MPP_CTX_DEC, MPP_CTX_ENC, MPP_CTX_ISP } MppCtxType;
typedef enum {
    MPP_VIDEO_CodingAVC,      // H.264
    MPP_VIDEO_CodingHEVC,     // H.265
    MPP_VIDEO_CodingVP8, MPP_VIDEO_CodingVP9, MPP_VIDEO_CodingAV1,
    MPP_VIDEO_CodingMJPEG, MPP_VIDEO_CodingMPEG2, MPP_VIDEO_CodingMPEG4,
} MppCodingType;
```

### 2.3 像素格式（常用）

| 格式 | 值 | 说明 |
|------|-----|------|
| MPP_FMT_YUV420SP | 0x00 | NV12，**解码默认输出** |
| MPP_FMT_YUV420P | 0x04 | I420 |
| MPP_FMT_YUV420SP_VU | 0x05 | NV21 |
| MPP_FMT_RGB888 | 0x100006 | 24-bit RGB |
| MPP_FMT_RGBA8888 | 0x10000D | 32-bit RGBA |

---

## 三、解码器使用流程（推荐新 API）

> **关键**: 用 `mpp_dec_cfg` 新 API 配置 `split_parse=1`，而非旧的 `MPP_DEC_SET_PARSER_SPLIT_MODE`。
> **关键**: put/get 必须用 do-while 循环处理 `MPP_ERR_BUFFER_FULL`(-1012)。

```c
#include "rk_mpi.h"

// 1. 创建 + 初始化
MppCtx ctx = NULL;  MppApi *mpi = NULL;
mpp_create(&ctx, &mpi);
mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingHEVC);

// 2. 启用内部分帧 (新 API)
MppDecCfg cfg;
mpp_dec_cfg_init(&cfg);
mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
mpi->control(ctx, MPP_DEC_SET_CFG, cfg);
mpp_dec_cfg_deinit(cfg);

// 3. 解码循环 (do-while 处理 BUFFER_FULL)
while (has_data) {
    MppPacket packet;
    mpp_packet_init(&packet, stream_data, data_size);

    MPP_RET ret;
    do {
        ret = mpi->decode_put_packet(ctx, packet);
        if (ret == MPP_ERR_BUFFER_FULL) {
            MppFrame f = NULL;
            mpi->decode_get_frame(ctx, &f);
            if (f) mpp_frame_deinit(&f);
        }
    } while (ret == MPP_ERR_BUFFER_FULL);

    // 取所有已解码帧
    MppFrame frame = NULL;
    do {
        mpi->decode_get_frame(ctx, &frame);
        if (!frame) break;

        if (mpp_frame_get_info_change(frame)) {
            // split_parse=1 后 MPP 用内部 buffer group, 无需手动配置
            mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
            mpp_frame_deinit(&frame);
            continue;
        }

        MppBuffer buffer = mpp_frame_get_buffer(frame);
        void *nv12_data = mpp_buffer_get_ptr(buffer);  // NV12 数据
        // → RGA 转 RGB / RKNN 推理 / 显示

        mpp_frame_deinit(&frame);
    } while (1);
    mpp_packet_deinit(&packet);
}
mpp_destroy(ctx);
```

---

## 四、Info Change 处理

```c
if (mpp_frame_get_info_change(frame)) {
    RK_U32 width      = mpp_frame_get_width(frame);
    RK_U32 height     = mpp_frame_get_height(frame);
    RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
    RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
    MppFrameFormat fmt = mpp_frame_get_fmt(frame);
    size_t buf_size   = mpp_frame_get_buf_size(frame);

    // split_parse=1 时: MPP 内部 buffer group, 只需通知已处理
    mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);

    // 旧模式 (无 split_parse): 需手动创建 limit buffer 组
    // MppBufferGroup group = NULL;
    // mpp_buffer_group_get(&group, MPP_BUFFER_TYPE_ION, MPP_BUFFER_LIMIT);
    // mpp_buffer_group_limit_config(group, buf_size, 20);  // H.265 需 20+ buffer
    // mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, group);
    // mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
}
```

---

## 五、DMA-BUF fd 零拷贝（关键）

```c
// 解码后获取 DMA-BUF fd, 无需 mpp_buffer_get_ptr 拷贝
MppBuffer frm_buf = mpp_frame_get_buffer(frame);
int mpp_fd = mpp_buffer_get_fd(frm_buf);      // DMA-BUF fd
int buf_size = mpp_buffer_get_size(frm_buf);
int w = mpp_frame_get_width(frame);
int h = mpp_frame_get_height(frame);
int hs = mpp_frame_get_hor_stride(frame);     // 水平 stride
int vs = mpp_frame_get_ver_stride(frame);     // 垂直 stride
// mpp_fd → RGA importbuffer_fd → RKNN (全链路零拷贝)
```

> 完整零拷贝管线见 `dma-buf-zero-copy.md`。

---

## 六、编码器使用流程

```c
mpp_create(&ctx, &mpi);
mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingHEVC);

MppEncCfg cfg;
mpp_enc_cfg_init(&cfg);
mpp_enc_cfg_set_s32(cfg, "prep:width", 1920);
mpp_enc_cfg_set_s32(cfg, "prep:height", 1080);
mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP);
mpp_enc_cfg_set_s32(cfg, "rc:rc_mode", 2);          // AVBR (推荐)
mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 4000000);
mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);
mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 30);
mpp_enc_cfg_set_s32(cfg, "rc:fps_in_den", 1);
mpp_enc_cfg_set_s32(cfg, "h264:profile", 1);        // 0:baseline 1:main 2:high
mpi->control(ctx, MPP_ENC_SET_CFG, cfg);

// 获取 SPS/PPS
MppPacket pkt;
mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, &pkt);
mpp_packet_deinit(&pkt);

// 编码循环
while (has_frame) {
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, 1920);
    mpp_frame_set_height(frame, 1080);
    mpp_frame_set_buffer(frame, input_buffer);  // 需 MppBuffer, 非 CPU malloc
    mpp_frame_set_pts(frame, pts);
    mpi->encode_put_frame(ctx, frame);
    mpp_frame_deinit(&frame);
    mpi->encode_get_packet(ctx, &packet);
    if (packet) { /* 取编码数据 */ mpp_packet_deinit(&packet); }
}
mpp_enc_cfg_deinit(cfg);
mpp_destroy(ctx);
```

### 码率控制模式

| 模式 | rc_mode | 说明 |
|------|---------|------|
| VBR | 0 | 可变码率 |
| CBR | 1 | 固定码率 |
| AVBR | 2 | 自适应可变码率（推荐） |
| FixQP | 3 | 固定 QP |

---

## 七、错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| MPP_OK | 0 | 成功 |
| MPP_ERR_INIT | -1002 | 初始化失败 |
| MPP_ERR_VPU_CODEC_INIT | -1003 | VPU 编解码初始化失败 |
| MPP_ERR_STREAM | -1004 | 码流错误 |
| MPP_ERR_VPUHW | -1009 | VPU 硬件错误 |
| MPP_EOS_STREAM_REACHED | -1011 | 码流结束 |
| MPP_ERR_BUFFER_FULL | -1012 | **缓冲区满（do-while 处理）** |

---

## 八、MPP 测试工具（板载）

```bash
# 硬解码测试 (H.264 type=7, H.265 type=16777220, JPEG type=8)
mpi_dec_test -i input.h264 -t 7 -o output.yuv -n 10 -v f

# 硬编码测试 (需 NV12 YUV 输入, -f 23)
mpi_enc_test -i input.yuv -w 640 -h 480 -t 7 -f 23 -o output.h264 -n 10 -v f

# MPP 版本信息
mpp_info_test
```

### 编解码类型 ID 速查

| 格式 | 解码 ID | 编码 ID |
|------|---------|---------|
| H.264/AVC | 7 | 7 |
| H.265/HEVC | 16777220 | 16777220 |
| VP8 | 9 | 9 |
| VP9 | 10 | - |
| JPEG | 8 | 8 |
| AV1 | 16777224 | - |

---

## 九、关键注意事项

1. **MPP 输入是裸码流**: 不含封装信息，需先解封装 (ffmpeg/自解析)，不能直接喂 MP4/MKV/RTSP URL
2. **内部分帧新 API**: `mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1)` + `MPP_DEC_SET_CFG`
3. **do-while put/get 循环**: `decode_put_packet` 可能返回 `MPP_ERR_BUFFER_FULL`(-1012)
4. **split_parse=1 后无需手动 buffer group**: Info Change 时只需 `MPP_DEC_SET_INFO_CHANGE_READY`
5. **编码器输入需 MppBuffer**: 不支持 CPU malloc 内存，需 ion/drm buffer 实现零拷贝
6. **H.264 输出带起始码**: 硬件固定输出 `00 00 00 01` 起始码
7. **H.264/H.265 需 20+ buffer**: 参考帧多，其他格式 10+ 即可
8. **NV12 是解码默认输出**: 需 RGA 转 RGB 给 RKNN/显示
9. **DMA-BUF fd 零拷贝**: `mpp_buffer_get_fd(buffer)` 获取 fd，直接传给 RGA
10. **EOS 后需 reset**: 解码器收到 EOS 后不再接收新码流，需 reset 恢复