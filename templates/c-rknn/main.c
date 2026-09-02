/*
 * Template: c-rknn
 * Goal: Run a converted RKNN model on NPU. Single-core by default; for
 *       multi-core SoCs (RK3576 dual / RK3588 triple) see the core-mask
 *       section in references/rknn-api.md.
 * Deps: librknnrt.so (aarch64) or librknnmrt.so (RV1106 armhf).
 *       Link: -lrknnrt  (or -lrknnmrt on RV1106)
 * Build (cross): aarch64-linux-gnu-gcc -O2 main.c -lrknnrt -o rknn_demo
 * Pitfalls:
 *  - MUST call rknn_destroy() before exit or NPU context leaks (board reboot
 *    needed on old BSP). See references/rknn-api.md pitfall #1.
 *  - RV1106 uses librknnmrt.so (note the 'm' = micro), different ABI.
 *  - For zero-copy input from RGA/DMA-BUF, use rknn_set_io_mem() with
 *    rknn_tensor_mem.fd instead of rknn_inputs_set(). See
 *    references/dma-buf-zero-copy.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include "rknn_api.h"

static void dump_inputs_outputs(rknn_context ctx)
{
    rknn_input_output_num io;
    if (rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) < 0) {
        fprintf(stderr, "query IO num failed\n");
        return;
    }
    printf("inputs=%u outputs=%u\n", io.input_num, io.output_num);
}

int main(int argc, char **argv)
{
    const char *model = (argc > 1) ? argv[1] : "model.rknn";
    FILE *fp = fopen(model, "rb");
    if (!fp) { perror("fopen model"); return 1; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(sz);
    if (fread(buf, 1, sz, fp) != (size_t)sz) { perror("fread"); return 1; }
    fclose(fp);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, buf, sz, 0, NULL, NULL);
    if (ret < 0) { fprintf(stderr, "rknn_init fail=%d\n", ret); return 1; }

    dump_inputs_outputs(ctx);

    /* TODO: rknn_inputs_set / rknn_run / rknn_outputs_get */

    rknn_destroy(ctx);
    free(buf);
    return 0;
}