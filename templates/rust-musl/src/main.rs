// Template: rust-musl main
// Minimal static daemon. For RK lib FFI, see bindgen approach in
// knowledge/c-go-struct-align.md (Rust section).
//
// NOTE: signal handling here uses libc directly to avoid extra deps.
// For production, add signal-hook = "0.3" to Cargo.toml.
use std::sync::atomic::{AtomicBool, Ordering};

static STOP: AtomicBool = AtomicBool::new(false);

extern "C" fn handle_sig(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}

fn main() {
    // SIGINT/SIGTERM handler via libc (no extra crate needed)
    unsafe {
        let mut sa: libc::sigaction = std::mem::zeroed();
        sa.sa_sigaction = handle_sig as usize;
        libc::sigaction(libc::SIGINT, &sa, std::ptr::null_mut());
        libc::sigaction(libc::SIGTERM, &sa, std::ptr::null_mut());
    }

    eprintln!("rk-daemon pid={} started", std::process::id());
    while !STOP.load(Ordering::Relaxed) {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
    eprintln!("rk-daemon stopping");
}
