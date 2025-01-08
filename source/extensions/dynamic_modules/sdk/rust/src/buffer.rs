/// A buffer struct that represents a contiguous memory region owned by Envoy.
///
/// This is cloneable and copyable, but the underlying memory is not copied. It can be
/// thought of as an alias to &\[u8\] but the underlying memory is owned by Envoy.
//
// Implementation note: The lifetime parameter `'a` is used to ensure that the memory region pointed
// to by `raw_ptr` is valid. The invalidation can happen when the mutable
// methods of [`crate::EnvoyHttpFilter`] are called.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct EnvoyBuffer<'a> {
  raw_ptr: *const u8,
  length: usize,
  _marker: std::marker::PhantomData<&'a ()>,
}

impl EnvoyBuffer<'_> {
  /// This is a helper function to create an [`EnvoyBuffer`] from a static string.
  ///
  /// This is meant for use by the end users in unit tests.
  // TODO: relax the lifetime constraint to 'static, so that it becomes more flexible.
  pub fn new(static_str: &'static str) -> Self {
    Self {
      raw_ptr: static_str.as_ptr(),
      length: static_str.len(),
      _marker: std::marker::PhantomData,
    }
  }

  /// This is a helper function to create an [`EnvoyBuffer`] from a raw pointer and length.
  ///
  /// # Safety
  ///
  /// This is not meant to be used by the end users, but rather by the SDK itself in the
  /// actual integration with Envoy.
  pub unsafe fn new_from_raw(raw_ptr: *const u8, length: usize) -> Self {
    Self {
      raw_ptr,
      length,
      _marker: std::marker::PhantomData,
    }
  }

  pub fn as_slice(&self) -> &[u8] {
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }

  pub fn as_mut_slice(&mut self) -> &mut [u8] {
    unsafe { std::slice::from_raw_parts_mut(self.raw_ptr as *mut u8, self.length) }
  }
}
