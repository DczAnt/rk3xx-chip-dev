/*
 * Template: cpp-mpp-rknn-rga
 * Goal: Full pipeline: MPP decode -> RGA resize/colorspace -> RKNN infer.
 *       Classic object detection / face recognition pipeline.
 * Deps: librockchip_mpp*, librga.so, librknnrt.so
 *       Link: -lrockchip_mpp -lrockchip_mpp_v2 -lrga -lrknnrt
 * Build (cross): aarch64-linux-gnu-g++ -O2 main.cpp \
 *                  -lrockchip_mpp -lrockchip_mpp_v2 -lrga -lrknnrt -o pipeline
 * Pitfalls:
 *  - RGA: use imresize + imcvtcolor COMBINED im2d API, NOT separate calls
 *    (separate calls allocate intermediate buffer, defeats zero-copy).
 *    See references/rga-api.md pitfall #1.
 *  - RGA: check return == IM_STATUS_SUCCESS (0), NOT just < 0. Positive
 *    non-zero values are also errors on some BSP versions.
 *  - RGA: wrapbuffer_handle_t stride MUST match MPP frame stride, NOT width.
 *    See references/rga-api.md pitfall #3.
 *  - For TRUE zero-copy (no memcpy at all), use the cpp-zero-copy template
 *    which threads DMA-BUF fd through the whole pipeline.
 *  - Multi-core NPU: set core_mask via rknn_set_core_mask() for RK3576/RK3588.
 *    See references/rknn-api.md "Multi-core scheduling".
 */
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "rk_mpi.h"
#include "mpp_err.h"
#include "im2d.h"
#include "RgaUtils.h"
#include "rknn_api.h"

int main(int argc, char **argv)
{
    const char *h264 = (argc > 1) ? argv[1] : "input.h264";
    const char *rknn = (argc > 2) ? argv[2] : "model.rknn";

    /* 1. MPP decode -> MppFrame */
    MppCtx dec; MppApi *api;
    mpp_create(&dec, &api);
    api->control(dec, MPP_DEC_SET_PARSER_SPLIT_MODE, NULL);
    mpp_init(dec, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    /* ... decode loop, get MppFrame frame ... */
    MppFrame frame = nullptr;
    (void)h264;

    /* 2. RGA resize+colorspace: NV12 -> RGB888 for RKNN */
    /* TODO: after decode, get frame info */
    int src_w = 1920, src_h = 1080, src_stride = 1920;
    int dst_w = 640,  dst_h = 640;
    rga_buffer_handle_t src = wrapbuffer_handle(
        mpp_buffer_get_fd(mpp_frame_get_buffer(frame)),  /* DMA-BUF fd */
        src_w, src_h, RK_FORMAT_YCbCr_420_SP, src_stride);
    rga_buffer_handle_t dst = wrapbuffer_virtualaddr(
        NULL, dst_w, dst_h, RK_FORMAT_BGR_888);
    im_rect srect = {0, 0, src_w, src_h};
    im_rect drect = {0, 0, dst_w, dst_h};
    /* COMBINED call: resize + colorspace in one improcess */
    int r = imresize(src, dst, &srect, &drect, 0)
          | imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888);
    if (r != IM_STATUS_SUCCESS) { fprintf(stderr, "rga fail=%d\n", r); }

    /* 3. RKNN infer */
    FILE *mf = fopen(rknn, "rb");
    fseek(mf, 0, SEEK_END); long msz = ftell(mf); fseek(mf, 0, SEEK_SET);
    unsigned char *mbuf = (unsigned char *)malloc(msz);
    fread(mbuf, 1, msz, mf); fclose(mf);
    rknn_context ctx = 0;
    rknn_init(&ctx, mbuf, msz, 0, NULL, NULL);
    /* TODO: rknn_inputs_set with RGA output, rknn_run, rknn_outputs_get */

    rknn_destroy(ctx);
    mpp_destroy(dec);
    return 0;
}