// Template: go-cgo-static
// Goal: Go service with cgo calling RK C libs, statically linked.
//       Use for HTTP/WebSocket/MQTT services that need NPU/MPP access.
// Build: CGO_ENABLED=1 CGO_LDFLAGS="-lrknnrt -lrga" \
//        go build -o service -tags "static -extldflags -static"
//
// Pitfalls (see knowledge/go-memory-gc.md, knowledge/c-go-struct-align.md):
//  - Go GC does NOT see C-allocated memory. Use runtime.KeepAlive() for
//    buffers passed to C that C holds asynchronously.
//  - C struct passed via cgo MUST be aligned identically. Use #pragma pack
//    or explicit padding. See c-go-struct-align.md.
//  - -static + cgo + glibc: NSS/dlopen may break. For network services,
//    prefer dynamic link on glibc >= 2.34 boards (RK3576/RK3588).
//  - For long-running daemons, see knowledge/systemd-watchdog.md for
//    sd_notify + hardware watchdog integration.
package main

/*
#cgo LDFLAGS: -lrknnrt
#include "rknn_api.h"
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"unsafe"
)

func loadModel(path string) (C.rknn_context, error) {
	// In production: read file via os.ReadFile, pass bytes to C.rknn_init
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var ctx C.rknn_context
	// Simplified: real code reads file into []byte, passes to rknn_init
	ret := C.rknn_init(&ctx, unsafe.Pointer(cpath), C.size_t(len(path)), 0, nil, nil)
	if ret < 0 {
		return 0, fmt.Errorf("rknn_init failed: %d", ret)
	}
	return ctx, nil
}

func main() {
	ctx, err := loadModel("model.rknn")
	if err != nil {
		fmt.Println("err:", err)
		return
	}
	defer C.rknn_destroy(ctx)
	fmt.Println("rknn ready")
	// TODO: HTTP/WebSocket service, see knowledge/systemd-watchdog.md
}