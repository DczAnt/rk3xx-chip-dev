# DRM/KMS VOP2 显示输出参考

> VOP2 是 Rockchip 显示控制器，通过 DRM/KMS 接口操作 plane/CRTC/connector。
> 本文档覆盖 RK3XX 通用 DRM 显示方案。plane ID **因芯片而异**，必须自动探测，不要硬编码。

## 一、VOP2 Plane 探测

```c
#include <xf86drm.h>
#include <xf86drmMode.h>

int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
drmModeRes *res = drmModeGetResources(fd);

// 找到 HDMI 连接器和 CRTC
drmModeConnector *conn = NULL;
for (int i = 0; i < res->count_connectors; i++) {
    conn = drmModeGetConnector(fd, res->connectors[i]);
    if (conn->connection == DRM_MODE_CONNECTED && strstr(conn->name, "HDMI")) break;
    drmModeFreeConnector(conn);
}

// 枚举 plane，自动识别 Smart/Esmart/Cluster
drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
for (int i = 0; i < plane_res->count_planes; i++) {
    drmModePlane *plane = drmModeGetPlane(fd, plane_res->planes[i]);
    // 检查 plane 支持的格式 (XRGB8888/ARGB8888/NV12)
    // 检查 plane 类型 (Primary/Overlay)
    // ⚠️ Cluster plane 仅支持 AFBC, 不支持 LINEAR dumb buffer, 避开
}
```

### Plane 类型与用途

| Plane 类型 | 格式 | LINEAR 支持 | 用途 |
|-----------|------|------------|------|
| Smart (Primary) | XRGB8888 | ✓ | **视频层** (zpos=0) |
| Esmart (Overlay) | ARGB8888/NV12 | ✓ | **OSD 层** (zpos=1, alpha 混合) |
| Cluster | XRGB8888/YU08 | ✗ 仅 AFBC | **不可用** (dumb buffer SetPlane 返回 -22) |

> **关键**: plane ID 不要硬编码！RK3568 是 57/129，RK3576 是 108/148，代码应按类型自动探测。

---

## 二、DRM 显示初始化

```c
// 1. 设置 mode
drmModeSetCrtc(fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &mode);

// 2. 创建 dumb buffer (XRGB8888)
struct drm_mode_create_dumb create = { .width = 1920, .height = 1080, .bpp = 32 };
drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

// 3. mmap dumb buffer
struct drm_mode_map_dumb mreq = { .handle = create.handle };
drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
uint8_t *map = mmap(0, create.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mreq.offset);

// 4. 创建 framebuffer
uint32_t fb_id;
drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, &handle, &pitch, &offset, &fb_id, 0);

// 5. SetPlane (视频层 zpos=0, OSD层 zpos=1)
drmModeSetPlane(fd, plane_id, crtc_id, fb_id, 0,
                crtc_x, crtc_y, crtc_w, crtc_h,    // 目标位置 (CRTC坐标)
                0, 0, src_w << 16, src_h << 16);    // 源位置 (16.16定点)
```

---

## 三、RGA 合成方案（多路 → 单平面）

```
N路 NV12 (MPP/ffmpeg 解码输出)
    ↓ RGA importbuffer_fd (DMA-BUF 零拷贝)
    ↓ RGA improcess (NV12 → XRGB8888, 缩放+拼接)
    ↓ 合成到单个 1920×1080 XRGB8888 DMA-BUF
    ↓ DRM dumb buffer SetPlane (Smart plane, zpos=0)
    ↓ HDMI 输出
```

### 关键点

- N 路各缩放到子尺寸，拼接为网格（如 4 路 960×540 → 2×2 网格 1920×1080）
- RGA `importbuffer_fd` 接受解码输出的 DMA-BUF fd，零拷贝
- 合成目标用 `wrapbuffer_handle_t`（6 参数版本，传入实际 stride）
- 格式转换: NV12 → XRGB8888 (RGA 硬件加速)

> **为何不能多路独立 plane**: HDMI 通常只有 3 个 plane（且 Cluster 不可用），多路独立 NV12 直显 plane 不足，必须 RGA 合成。

---

## 四、OSD 双缓冲（消除闪烁）

```c
// 双缓冲: 两个 dumb buffer 交替使用
uint32_t osd_fb_id[2], osd_handle[2];
uint8_t *osd_buf[2];
int osd_idx = 0;

// 每帧:
osd_idx = 1 - osd_idx;  // 切换缓冲
// CPU 绘制 OSD 到 osd_buf[osd_idx] (ARGB8888)
draw_osd(osd_buf[osd_idx], w, h, fps, persons, ...);
// SetPlane 显示新缓冲
drmModeSetPlane(fd, osd_plane_id, crtc_id, osd_fb_id[osd_idx], ...);
// 旧缓冲自然不再显示，无撕裂/闪烁
```

### OSD 绘制 (CPU, ARGB8888)

- 点阵字体，内容: 标签 + FPS + 检测人数 + 性能条
- ARGB8888 格式: 每像素 4 字节 (A,R,G,B)，alpha 混合由硬件完成
- 透明背景: alpha=0，文字区域 alpha=255

---

## 五、编译依赖

```bash
# 板上安装
apt install libdrm-dev

# 编译链接
gcc -o display display.c -ldrm -I/usr/include/libdrm
```

---

## 六、常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| SetPlane 返回 -22 | Cluster plane 不支持 LINEAR | 改用 Smart/Esmart plane |
| 画面闪烁 | 单缓冲撕裂 | 双缓冲交替显示 |
| OSD 不透明 | alpha 通道未设置 | ARGB8888 格式，背景 alpha=0 |
| 多路无法独立 plane | plane 数量不足 | RGA 合成到单 plane |
| dumb buffer mmap 失败 | 未 DRM_IOCTL_MODE_MAP_DUMB | 先 map_dumb 再 mmap |
| SetPlane 源坐标错误 | 需要 16.16 定点格式 | `src_w << 16, src_h << 16` |
| plane ID 不匹配 | 硬编码了特定芯片的 ID | 自动探测 Smart/Esmart plane |