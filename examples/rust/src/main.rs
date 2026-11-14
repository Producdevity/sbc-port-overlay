// SPDX-License-Identifier: MIT
use std::ffi::{c_char, c_void};
type GetProc = unsafe extern "C" fn(*const c_char, *mut c_void) -> *mut c_void;
type Pause = unsafe extern "C" fn(u32, *mut c_void);
#[repr(C)]
struct SpoConfig {
    struct_size: u32,
    abi_version: u32,
    get_proc_address: Option<GetProc>,
    gl_user: *mut c_void,
    config_path: *const c_char,
    port_name: *const c_char,
    port_version: *const c_char,
    on_pause: Option<Pause>,
    pause_user: *mut c_void,
    flags: u32,
}
unsafe extern "C" {
    fn spo_init(config: *const SpoConfig) -> i32;
    fn spo_frame(width: u32, height: u32) -> i32;
    fn spo_sample() -> i32;
    fn spo_shutdown();
}
fn main() {
    let config = SpoConfig {
        struct_size: std::mem::size_of::<SpoConfig>() as u32,
        abi_version: 1,
        get_proc_address: None,
        gl_user: std::ptr::null_mut(),
        config_path: std::ptr::null(),
        port_name: c"Rust integration".as_ptr(),
        port_version: c"1.0".as_ptr(),
        on_pause: None,
        pause_user: std::ptr::null_mut(),
        flags: 3,
    };
    unsafe {
        assert_eq!(spo_init(&config), 0);
        assert_eq!(spo_sample(), 0);
        let result = spo_frame(640, 480);
        spo_shutdown();
        assert_eq!(result, 0);
    }
}
