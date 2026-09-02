/*
 * Template: c-static
 * Goal: Pure C static binary, no RK SDK dependency.
 *       Use for board-side utilities, daemons, test harness that do not
 *       touch MPP/RKNN/RGA/VPU/DRM. Smallest footprint, widest portability.
 * Build (cross): aarch64-linux-gnu-gcc -static -O2 main.c -o app
 * Build (native): gcc -static -O2 main.c -o app
 * Note: -static + glibc >= 2.34 may hit NSS/dlopen warnings; for daemons
 *       that only use read/write/socket, static is safe. See
 *       knowledge/cross-compile-glibc.md for the glibc compatibility matrix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

int main(int argc, char **argv)
{
    const char *out = (argc > 1) ? argv[1] : "/tmp/app.out";
    FILE *f = fopen(out, "w");
    if (!f) { perror("fopen"); return 1; }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    fprintf(f, "pid=%ld\n", (long)getpid());
    fprintf(f, "arch=%d-bit\n", (int)(sizeof(void *) * 8));

    while (!g_stop) {
        fprintf(f, "tick\n");
        fflush(f);
        sleep(1);
    }
    fprintf(f, "bye\n");
    fclose(f);
    return 0;
}