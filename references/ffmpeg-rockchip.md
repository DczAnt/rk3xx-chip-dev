# ffmpeg-rockchip 使用指南

> ffmpeg-rockchip 是 Rockchip 维护的 ffmpeg 分支，支持 rkmpp 硬件编解码和 rkrga 硬件滤镜。
> 安装路径因芯片而异，见 `boards/<soc>.md`。

## 一、支持的 rkmpp 组件

### 硬件编码器

| 编码器 | 命令名 | 芯片支持 |
|--------|--------|---------|
| H.264 硬编码 | h264_rkmpp | 全部 |
| H.265 硬编码 | hevc_rkmpp | 全部 |
| MJPEG 硬编码 | mjpeg_rkmpp | RK3576/RK3588/RV1106（RK3566/3568 无 jpege） |

### 硬件解码器

| 解码器 | 命令名 |
|--------|--------|
| AV1 硬解 | av1_rkmpp |
| H.264 硬解 | h264_rkmpp |
| H.265 硬解 | hevc_rkmpp |
| MJPEG 硬解 | mjpeg_rkmpp |
| VP8/VP9 硬解 | vp8_rkmpp / vp9_rkmpp |
| MPEG1/2/4 硬解 | mpeg1_rkmpp / mpeg2_rkmpp / mpeg4_rkmpp |

### RGA 硬件滤镜

| 滤镜 | 说明 | 输入格式要求 |
|------|------|-------------|
| scale_rkrga | RGA 视频缩放和格式转换 | DRM_PRIME |
| vpp_rkrga | RGA 视频后处理(缩放/裁剪/转置) | DRM_PRIME |
| overlay_rkrga | RGA 视频合成 | DRM_PRIME |

---

## 二、基本硬编解码命令

```bash
# H.264 硬解码
ffmpeg -c:v h264_rkmpp -i input.h264 -f null -

# H.264 硬编码 (YUV 转 H264)
ffmpeg -f rawvideo -pix_fmt nv12 -s 640x480 -i input.yuv \
       -c:v h264_rkmpp -b:v 2M -y output.mp4

# H.264→H.265 转码 (硬解+硬编)
ffmpeg -c:v h264_rkmpp -i input.h264 -c:v hevc_rkmpp -b:v 1M -y output.mp4

# MJPEG 硬编码 (仅 RK3576/RK3588/RV1106)
ffmpeg -f rawvideo -pix_fmt nv12 -s 640x480 -i input.yuv \
       -c:v mjpeg_rkmpp -b:v 2M -y output.mp4
```

---

## 三、RGA 硬件滤镜

### 关键要求：DRM_PRIME 格式

rkrga 滤镜**只接受 `AV_PIX_FMT_DRM_PRIME` 格式的硬件帧**输入。必须通过以下参数转换：

```bash
-init_hw_device rkmpp                    # 初始化 RKMPP 硬件设备
-hwaccel rkmpp                           # 使用 RKMPP 硬件加速
-hwaccel_output_format drm_prime         # 输出 DRM_PRIME 格式
```

### RGA 缩放

```bash
ffmpeg -init_hw_device rkmpp -hwaccel rkmpp -hwaccel_output_format drm_prime \
       -c:v h264_rkmpp -i input.h264 \
       -vf scale_rkrga=320:240 -c:v h264_rkmpp -b:v 500k -y output.mp4
```

### RGA 旋转

```bash
ffmpeg -init_hw_device rkmpp -hwaccel rkmpp -hwaccel_output_format drm_prime \
       -c:v h264_rkmpp -i input.h264 \
       -vf vpp_rkrga=transpose=clock -c:v h264_rkmpp -b:v 500k -y output.mp4
```

### scale_rkrga 参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| w / h | 输出宽高 | iw / ih |
| format | 输出像素格式 | none |
| force_chroma | 强制色度 (0:auto 1:420sp 2:420p 3:422sp 4:422p) | auto |
| async_depth | 并行深度 (0-4) | 2 |
| afbc | 启用 AFBC 压缩 | false |

---

## 四、完整流水线模板

### RTSP 拉流 + 硬解 + 硬编 + 推流

```bash
ffmpeg -c:v h264_rkmpp -i rtsp://camera_ip/stream \
       -c:v hevc_rkmpp -b:v 2M -f rtsp rtsp://server_ip/output
```

### 硬解 + RGA 硬件缩放 + 硬编 (全硬件流水线)

```bash
ffmpeg -init_hw_device rkmpp -hwaccel rkmpp -hwaccel_output_format drm_prime \
       -c:v h264_rkmpp -i input.h264 \
       -vf scale_rkrga=640:480 -c:v h264_rkmpp -b:v 1M -y output.mp4
```

---

## 五、C API 硬解（推荐，零拷贝管线）

> **优势**: 无管道 I/O，单进程，ffmpeg 内置硬解直接输出 DRM PRIME fd，可实现全链路零拷贝。
> **命令行无法导出 DRM PRIME 帧给外部程序，零拷贝管线只能用 C API。**

完整 C API 代码见 `dma-buf-zero-copy.md`。

---

## 六、命令行限制（重要）

| 限制 | 说明 | 替代方案 |
|------|------|---------|
| `hwdownload` 滤镜 | 板上报 "Function not implemented" | 用 C API |
| `scale_rkrga` 命令行 | 格式协商失败 "Impossible to convert between formats" | 用 C API |
| DRM PRIME 导出 | 命令行无法将 DRM PRIME 帧导出给外部程序 | 用 C API |

---

## 七、编译部署

### 编译环境要求

| 依赖 | 版本要求 |
|------|----------|
| gcc | 9.4.0+ |
| rockchip_mpp | >= 1.3.9 |
| libdrm | 2.4.107+ |
| librga | 2.1.0+ |

### 编译命令

```bash
cd /opt/ffmpeg-build
./configure --enable-rkmpp --enable-rkrga --enable-libdrm \
            --enable-nonfree --enable-gpl --enable-version3 \
            --prefix=/usr/local --disable-debug --disable-doc \
            --disable-ffplay --enable-ffmpeg --enable-ffprobe
make -j$(nproc)
make install
```

### Windows 源码传输注意

```bash
# 1. 排除 tests 和 doc
tar -czf ffmpeg-rockchip.tar.gz --exclude="tests" --exclude="doc" --exclude=".git" .

# 2. 修复 Windows 换行符 (CRLF→LF)
find . -type f \( -name '*.sh' -o -name '*.mak' -o -name 'configure' \) -exec sed -i 's/\r$//' {} \;

# 3. 修复权限
chmod +x configure
```

---

## 八、常见错误排查

| 错误 | 原因 | 解决 |
|------|------|------|
| rkrga 滤镜报 "Function not implemented" (-38) | 缺少 DRM_PRIME 格式参数 | 添加 `-init_hw_device rkmpp -hwaccel rkmpp -hwaccel_output_format drm_prime` |
| rkrga 滤镜报 "Invalid argument" (-22) | 编码器输入格式不匹配 | 添加 `force_chroma=420sp` 或检查输入格式 |
| MPP 解码报 "unsupported soc" | 输入是 MP4/MKV 容器格式 | 用 ffmpeg 提取裸流: `ffmpeg -i input.mp4 -an -c:v copy -bsf:v h264_mp4toannexb output.h264` |
| h264_rkmpp 编码器报 "Invalid argument" | 输入像素格式不是 NV12 | 添加 `-pix_fmt nv12` 或 `-vf format=nv12` |
| `rkmpp is version3` | 未启用 version3 | 添加 `--enable-version3` |
| `configure: /bin/sh^M: bad interpreter` | Windows 换行符 | `sed -i 's/\r$//' configure` |

---

## 九、验证命令

```bash
ffmpeg -version
ffmpeg -encoders | grep rkmpp
ffmpeg -decoders | grep rkmpp
ffmpeg -filters | grep rkrga
ffmpeg -h filter=scale_rkrga
```