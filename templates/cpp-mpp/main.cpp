/*
 * Template: cpp-mpp
 * Goal: Hardware H.264/H.265 decode via Rockchip MPP (Media Process Platform).
 *       Replace FFmpeg soft decode with VPU for 4K@60fps class streams.
 * Deps: librockchip_mpp.so, librockchip_mpp_v2.so (BSP >= 2023).
 *       Link: -lrockchip_mpp -lrockchip_mpp_v2
 * Build (cross): aarch64-linux-gnu-g++ -O2 main.cpp \
 *                  -lrockchip_mpp -lrockchip_mpp_v2 -o mpp_dec
 * Pitfalls (see references/mpp-api.md):
 *  - Use split_parse (new API) NOT mpp_packet_put; old API drops first frame.
 *  - do { ... } while(BUFFER_FULL) loop on send, NOT while(OK).
 *  - For JPEG encode, check board capability jpege first: RK3568 has no
 *    jpege, use mjpeg_rkmpp only on RK3576/RK3588/RV1106.
 *    See boards/registry.yaml + references/vpu-jpeg.md.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "rk_mpi.h"
#include "rk_vdec_cfg.h"
#include "mpp_err.h"

static MppCtx mpp_ctx = nullptr;
static MppApi *mpp_api = nullptr;

int main(int argc, char **argv)
{
    const char *file = (argc > 1) ? argv[1] : "input.h264";
    FILE *fp = fopen(file, "rb");
    if (!fp) { perror("fopen"); return 1; }

    MppCodingType type = MPP_VIDEO_CodingAVC;
    if (strstr(file, "265") || strstr(file, "hevc")) type = MPP_VIDEO_CodingHEVC;

    if (mpp_create(&mpp_ctx, &mpp_api) != MPP_OK) {
        fprintf(stderr, "mpp_create fail\n"); return 1;
    }
    /* 新 API: MPP_DEC_SET_CFG + split_parse
     * 来源: mpp/inc/rk_vdec_cfg.h, rk_mpi_cmd.h:114
     * 旧 API MPP_DEC_SET_PARSER_SPLIT_MODE 已废弃，见 SKILL.md 陷阱 #2 */
    MppDecCfg dec_cfg = NULL;
    mpp_dec_cfg_init(&dec_cfg);
    mpp_dec_cfg_set_u32(dec_cfg, "base:split_parse", 1);
    if (mpp_api->control(mpp_ctx, MPP_DEC_SET_CFG, dec_cfg) != MPP_OK) {
        fprintf(stderr, "set split_parse fail\n"); return 1;
    }
    mpp_dec_cfg_deinit(dec_cfg);
    if (mpp_init(mpp_ctx, MPP_CTX_DEC, type) != MPP_OK) {
        fprintf(stderr, "mpp_init fail\n"); return 1;
    }

    unsigned char buf[1024 * 1024];
    MppPacket packet = nullptr;
    do {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        mpp_packet_init(&packet, buf, n);
        mpp_packet_set_pos(packet, 0);
        mpp_packet_set_length(packet, n);

        /* do-while BUFFER_FULL: MUST retry on full, see mpp-api.md pitfall #2 */
        do {
            int ret = mpp_api->decode_put_packet(mpp_ctx, packet);
            if (ret == MPP_ERR_BUFFER_FULL) { usleep(1000); continue; }
            break;
        } while (true);

        MppFrame frame = nullptr;
        while (mpp_api->decode_get_frame(mpp_ctx, &frame) == MPP_OK && frame) {
            printf("got frame %dx%d\n",
                   mpp_frame_get_width(frame), mpp_frame_get_height(frame));
            mpp_frame_deinit(&frame);
        }
        mpp_packet_deinit(&packet);
    } while (true);

    fclose(fp);
    mpp_destroy(mpp_ctx);
    return 0;
}