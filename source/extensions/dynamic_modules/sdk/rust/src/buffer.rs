/// A buffer struct that represents a contiguous memory region owned by Envoy.
///
/// The life time (not in Rust sense) of the buffer is managed by Envoy, and it depends on how
/// this [`EnvoyBuffer`] is created, for example, via [`crate::EnvoyHttpFilter`]'s methods.
///
/// This is cloneable and copyable, but the underlying memory is not copied. It can be
/// thought of as an alias to &\[u8\] but the underlying memory is owned by Envoy.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct EnvoyBuffer {
  raw_ptr: *const u8,
  length: usize,
}

impl EnvoyBuffer {
  pub fn new(raw_ptr: *const u8, length: usize) -> Self {
    Self { raw_ptr, length }
  }

  pub fn as_slice(&self) -> &[u8] {
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }

  pub fn as_mut_slice(&mut self) -> &mut [u8] {
    unsafe { std::slice::from_raw_parts_mut(self.raw_ptr as *mut u8, self.length) }
  }
}
