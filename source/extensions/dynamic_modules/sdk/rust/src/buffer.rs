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
  /// This is a helper function to create an [`EnvoyBuffer`] from a static string.
  ///
  /// This is meant for use by the end users in unit tests.
  // TODO: relax the lifetime constraint to 'static, so that it becomes more flexible.
  pub fn new(static_str: &'static str) -> Self {
    Self {
      raw_ptr: static_str.as_ptr(),
      length: static_str.len(),
    }
  }

  /// This is a helper function to create an [`EnvoyBuffer`] from a raw pointer and length.
  ///
  /// # Safety
  ///
  /// This is not meant to be used by the end users, but rather by the SDK itself in the
  /// actual integration with Envoy.
  pub unsafe fn new_from_raw(raw_ptr: *const u8, length: usize) -> Self {
    Self { raw_ptr, length }
  }

  pub fn as_slice(&self) -> &[u8] {
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }

  pub fn as_mut_slice(&mut self) -> &mut [u8] {
    unsafe { std::slice::from_raw_parts_mut(self.raw_ptr as *mut u8, self.length) }
  }
}
