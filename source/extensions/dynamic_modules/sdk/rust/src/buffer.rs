/// A buffer struct that represents a contiguous memory region owned by Envoy.
/// This is immutable and is meant to be used for reading underlying bytes.
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

impl Default for EnvoyBuffer<'_> {
  fn default() -> Self {
    Self {
      raw_ptr: std::ptr::null(),
      length: 0,
      _marker: std::marker::PhantomData,
    }
  }
}

impl EnvoyBuffer<'_> {
  /// This is a helper function to create an [`EnvoyBuffer`] from a byte slice.
  ///
  /// This is meant for use by the end users in unit tests.
  pub fn new(s: &[u8]) -> Self {
    Self {
      raw_ptr: s.as_ptr(),
      length: s.len(),
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
    // The null guard is inlined here rather than going through `ffi_helpers` so that this
    // method does not transitively reference `envoy_log_*`. `as_slice` is reached by SDK
    // doc tests, which compile without `#[cfg(test)]` and so cannot link the host-provided
    // `envoy_dynamic_module_callback_log` symbols that the logging macros expand to.
    if self.raw_ptr.is_null() {
      return &[];
    }
    // Safety: caller invariant from `new` / `new_from_raw` that `(raw_ptr, length)` describes
    // a live region of `length` bytes for the buffer's lifetime.
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }
}

/// This is exactly the same as [`EnvoyBuffer`] except that it is mutable and data can be written
/// in-place. See [`EnvoyBuffer`] for more details.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct EnvoyMutBuffer<'a> {
  raw_ptr: *mut u8,
  length: usize,
  _marker: std::marker::PhantomData<&'a ()>,
}

impl EnvoyMutBuffer<'_> {
  /// This is a helper function to create an [`EnvoyMutBuffer`] from a raw pointer to static
  /// mutable storage. This is only meant to be used in unit tests.
  ///
  /// # Safety
  ///
  /// The caller must ensure that the pointer is valid for the lifetime of the returned buffer and
  /// that no other references to the data exist while the buffer is in use.
  ///
  /// ```
  /// static mut BUF: [u8; 1024] = [0; 1024];
  /// let _buffer = unsafe { envoy_proxy_dynamic_modules_rust_sdk::EnvoyMutBuffer::new(&raw mut BUF) };
  /// ```
  #[allow(unknown_lints, dangerous_implicit_autorefs)]
  pub unsafe fn new(static_buf: *mut [u8]) -> Self {
    Self {
      raw_ptr: static_buf as *mut u8,
      length: (*static_buf).len(),
      _marker: std::marker::PhantomData,
    }
  }

  /// This is a helper function to create an [`EnvoyMutBuffer`] from a raw pointer and length.
  ///
  /// # Safety
  ///
  /// This is not meant to be used by the end users, but rather by the SDK itself in the
  /// actual integration with Envoy.
  pub unsafe fn new_from_raw(raw_ptr: *mut u8, length: usize) -> Self {
    Self {
      raw_ptr,
      length,
      _marker: std::marker::PhantomData,
    }
  }

  /// This returns an immutable slice to the underlying memory region managed by Envoy.
  pub fn as_slice(&self) -> &[u8] {
    // See `EnvoyBuffer::as_slice` for why the null guard is inlined here.
    if self.raw_ptr.is_null() {
      return &[];
    }
    // Safety: caller invariant from `new` / `new_from_raw`.
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }

  /// This returns a mutable slice to the underlying memory region managed by Envoy.
  pub fn as_mut_slice(&mut self) -> &mut [u8] {
    // See `EnvoyBuffer::as_slice` for why the null guard is inlined here.
    if self.raw_ptr.is_null() {
      return &mut [];
    }
    // Safety: caller invariant from `new` / `new_from_raw`, plus exclusive borrow on
    // the underlying memory for the duration of the returned slice.
    unsafe { std::slice::from_raw_parts_mut(self.raw_ptr, self.length) }
  }
}

impl Default for EnvoyMutBuffer<'_> {
  fn default() -> Self {
    Self {
      raw_ptr: std::ptr::null_mut(),
      length: 0,
      _marker: std::marker::PhantomData,
    }
  }
}
