//! Resolves the Envoy callbacks of a Windows dynamic module DLL against the host executable.
//!
//! On Windows, the bindings import the Envoy callbacks from `envoy.dll` (see build.rs), since a DLL
//! cannot leave symbols undefined. The callbacks are actually exported by the host executable,
//! whose file name is not known when the module is built. The module DLL is therefore linked with
//! `/DELAYLOAD:envoy.dll`, which defers resolving these imports to the first call, and this hook
//! makes the delay-load helper bind them to the host executable instead of loading `envoy.dll`.

use std::ffi::{c_char, c_void, CStr};

// Provides the delay-load helper, __delayLoadHelper2.
#[link(name = "delayimp")]
extern "C" {}

#[link(name = "kernel32")]
extern "system" {
  fn GetModuleHandleW(module_name: *const u16) -> *mut c_void;
}

/// The leading fields of `DelayLoadInfo` from delayimp.h. Only `sz_dll` is read.
#[repr(C)]
struct DelayLoadInfo {
  cb: u32,
  pidd: *const c_void,
  ppfn: *mut *mut c_void,
  sz_dll: *const c_char,
}

/// `dliNotePreLoadLibrary` from delayimp.h.
const DLI_NOTE_PRE_LOAD_LIBRARY: u32 = 1;

/// Called by the delay-load helper before it loads a delay-loaded DLL. Returning a module handle
/// makes the helper use it instead of calling LoadLibrary.
unsafe extern "system" fn delay_load_notify_hook(
  notify: u32,
  info: *const DelayLoadInfo,
) -> *mut c_void {
  if notify == DLI_NOTE_PRE_LOAD_LIBRARY && !info.is_null() && !(*info).sz_dll.is_null() {
    let dll = CStr::from_ptr((*info).sz_dll);
    if dll.to_bytes().eq_ignore_ascii_case(b"envoy.dll") {
      // The handle of the executable that created the process, i.e. the host that loaded this
      // module.
      return GetModuleHandleW(std::ptr::null());
    }
  }
  std::ptr::null_mut()
}

/// The notification hook that the delay-load helper calls, see delayimp.h.
#[no_mangle]
#[used]
static __pfnDliNotifyHook2: unsafe extern "system" fn(u32, *const DelayLoadInfo) -> *mut c_void =
  delay_load_notify_hook;
