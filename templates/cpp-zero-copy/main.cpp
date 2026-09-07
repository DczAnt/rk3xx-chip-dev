/*
 * Template: cpp-zero-copy  (NEW, not in original skill)
 * Goal: TRUE zero-copy pipeline: MPP decode -> RGA -> RKNN, all via DMA-BUF fd.
 *       Zero memcpy in the hot path. For 4K@60fps / multi-camera pipelines
 *       where every memcpy costs ~8ms/frame.
 * Deps: librockchip_mpp*, librga.so, librknnrt.so, Linux dma-buf heaps.
 *       Link: -lrockchip_mpp -lrockchip_mpp_v2 -lrga -lrknnrt
 * Build (cross): aarch64-linux-gnu-g++ -O2 main.cpp \
 *                  -lrockchip_mpp -lrockchip_mpp_v2 -lrga -lrknnrt -o zerocopy
 *
 * Zero-copy chain (see references/dma-buf-zero-copy.md for full diagram):
 *   MppFrame -> mpp_buffer_get_fd() -> [fd0]
 *   [fd0] -> RGA importbuffer_fd -> improcess -> [fd1]  (RGA alloc dst)
 *   [fd1] -> RKNN set_io_mem(rknn_tensor_mem.fd=fd1) -> rknn_run
 *
 * Key APIs:
 *  - MPP:  mpp_buffer_get_fd(mpp_frame_get_buffer(frame))  -> DMA-BUF fd
 *  - RGA:  wrapbuffer_handle(fd, ...) + importbuffer_fd, or im2d API auto-import
 *  - RKNN: rknn_set_io_mem(ctx, &mem, &tensor_attr) where mem.fd = fd1
 *
 * Pitfalls (see references/dma-buf-zero-copy.md):
 *  - RKNN set_io_mem requires rknn_init flag RKNN_FLAG_MEM_ALLOC_OUTSIDE.
 *  - fd ownership: MPP owns fd0, RGA owns fd1. Do NOT close them. RKNN does
 *    NOT dup the fd, so fd1 must outlive the rknn_run() call.
 *  - On RV1106 (armhf), dma-buf heaps may be /dev/dma_heap/system, not
 *    /dev/dma_heap/system-uncached. Check boards/rv1106.md.
 *  - VOP2 display can also import fd1 directly for OSD/layer preview,
 *    see references/drm-vop2.md "dma-buf import".
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "rk_mpi.h"
#include "rk_vdec_cfg.h"
#include "mpp_err.h"
#include "im2d.h"
#include "RgaUtils.h"
#include "rknn_api.h"

int main(int argc, char **argv)
{
    const char *h264 = (argc > 1) ? argv[1] : "input.h264";
    const char *rknn = (argc > 2) ? argv[2] : "model.rknn";
    (void)h264;

    /* 1. RKNN init with OUTSIDE flag (required for set_io_mem) */
    FILE *mf = fopen(rknn, "rb");
    fseek(mf, 0, SEEK_END); long msz = ftell(mf); fseek(mf, 0, SEEK_SET);
    unsigned char *mbuf = (unsigned char *)malloc(msz);
    fread(mbuf, 1, msz, mf); fclose(mf);
    rknn_context ctx = 0;
    rknn_init(&ctx, mbuf, msz, RKNN_FLAG_MEM_ALLOC_OUTSIDE, NULL, NULL);

    /* 2. MPP decode */
    MppCtx dec; MppApi *api;
    mpp_create(&dec, &api);
    /* 新 API split_parse，见 SKILL.md 陷阱 #2 */
    MppDecCfg dec_cfg = NULL;
    mpp_dec_cfg_init(&dec_cfg);
    mpp_dec_cfg_set_u32(dec_cfg, "base:split_parse", 1);
    api->control(dec, MPP_DEC_SET_CFG, dec_cfg);
    mpp_dec_cfg_deinit(dec_cfg);
    mpp_init(dec, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);

    /* 3. Allocate RGA dst buffer ONCE (reused across frames) */
    int dst_w = 640, dst_h = 640;
    /* Allocate via dma_heap for true zero-copy (or let RGA alloc) */
    int dst_fd = -1;  /* TODO: open /dev/dma_heap/system + ioctl alloc */
    rga_buffer_handle_t dst = wrapbuffer_handle(dst_fd, dst_w, dst_h,
                                                RK_FORMAT_BGR_888);

    /* 4. Pipeline loop */
    MppFrame frame = nullptr;
    while (api->decode_get_frame(dec, &frame) == MPP_OK && frame) {
        /* 4a. MPP frame -> DMA-BUF fd (zero-copy, no memcpy) */
        int src_fd = mpp_buffer_get_fd(mpp_frame_get_buffer(frame));
        int src_w = mpp_frame_get_width(frame);
        int src_h = mpp_frame_get_height(frame);
        int src_stride = mpp_frame_get_hor_stride(frame);  /* NOT width! */

        rga_buffer_handle_t src = wrapbuffer_handle(src_fd, src_w, src_h,
                                                    RK_FORMAT_YCbCr_420_SP,
                                                    src_stride);
        /* 4b. RGA: resize + NV12->BGR via improcess (zero-copy, fd->fd)
         * 来源: librga/include/im2d_single.h:502
         * imresize 无 rect 参数，resize+cvtcolor 须用 improcess */
        rga_buffer_t pat = {};
        im_rect prect = {0, 0, 0, 0};
        im_rect srect = {0, 0, src_w, src_h};
        im_rect drect = {0, 0, dst_w, dst_h};
        int r = improcess(src, dst, pat, srect, drect, prect, -1, NULL, NULL, IM_SYNC);
        if (r != IM_STATUS_SUCCESS) {
            fprintf(stderr, "rga fail=%d\n", r);
            mpp_frame_deinit(&frame);
            continue;
        }

        /* 4c. RKNN: set_io_mem with fd (zero-copy, no memcpy) */
        rknn_tensor_mem mem;
        memset(&mem, 0, sizeof(mem));
        mem.fd = dst_fd;  /* RGA output fd, RKNN reads directly */
        mem.size = dst_w * dst_h * 3;
        rknn_tensor_mem_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.index = 0;
        attr.type = RKNN_TENSOR_MEMORY;
        /* TODO: query attr from rknn_query first */
        rknn_set_io_mem(ctx, &mem, &attr);

        rknn_run(ctx, NULL);

        /* 4d. Get output (output is RKNN-allocated, read-only) */
        rknn_output out;
        memset(&out, 0, sizeof(out));
        out.want_float = 0;
        out.is_prealloc = 0;
        rknn_outputs_get(ctx, 1, &out, NULL);
        /* TODO: post-process out.buf */
        rknn_outputs_release(ctx, 1, &out);

        mpp_frame_deinit(&frame);
    }

    rknn_destroy(ctx);
    mpp_destroy(dec);
    free(mbuf);
    return 0;
}