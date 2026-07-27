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

  #[inline]
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
  #[inline]
  pub fn as_slice(&self) -> &[u8] {
    // See `EnvoyBuffer::as_slice` for why the null guard is inlined here.
    if self.raw_ptr.is_null() {
      return &[];
    }
    // Safety: caller invariant from `new` / `new_from_raw`.
    unsafe { std::slice::from_raw_parts(self.raw_ptr, self.length) }
  }

  /// This returns a mutable slice to the underlying memory region managed by Envoy.
  #[inline]
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

// Envoy fills caller-allocated `Vec`s of these types in place by reinterpreting them as the ABI
// buffer and HTTP header structs, so assert the layouts match to keep those reinterpretations sound.
const _: () = {
  type EnvoyBufferPair = (EnvoyBuffer<'static>, EnvoyBuffer<'static>);

  assert!(
    std::mem::size_of::<EnvoyBuffer<'static>>()
      == std::mem::size_of::<crate::abi::envoy_dynamic_module_type_envoy_buffer>()
  );
  assert!(
    std::mem::align_of::<EnvoyBuffer<'static>>()
      == std::mem::align_of::<crate::abi::envoy_dynamic_module_type_envoy_buffer>()
  );
  assert!(
    std::mem::offset_of!(EnvoyBuffer<'static>, raw_ptr)
      == std::mem::offset_of!(crate::abi::envoy_dynamic_module_type_envoy_buffer, ptr)
  );
  assert!(
    std::mem::offset_of!(EnvoyBuffer<'static>, length)
      == std::mem::offset_of!(crate::abi::envoy_dynamic_module_type_envoy_buffer, length)
  );

  assert!(
    std::mem::size_of::<EnvoyMutBuffer<'static>>()
      == std::mem::size_of::<crate::abi::envoy_dynamic_module_type_envoy_buffer>()
  );
  assert!(
    std::mem::align_of::<EnvoyMutBuffer<'static>>()
      == std::mem::align_of::<crate::abi::envoy_dynamic_module_type_envoy_buffer>()
  );
  assert!(
    std::mem::offset_of!(EnvoyMutBuffer<'static>, raw_ptr)
      == std::mem::offset_of!(crate::abi::envoy_dynamic_module_type_envoy_buffer, ptr)
  );
  assert!(
    std::mem::offset_of!(EnvoyMutBuffer<'static>, length)
      == std::mem::offset_of!(crate::abi::envoy_dynamic_module_type_envoy_buffer, length)
  );

  assert!(
    std::mem::size_of::<EnvoyBufferPair>()
      == std::mem::size_of::<crate::abi::envoy_dynamic_module_type_envoy_http_header>()
  );
  assert!(
    std::mem::align_of::<EnvoyBufferPair>()
      == std::mem::align_of::<crate::abi::envoy_dynamic_module_type_envoy_http_header>()
  );
  assert!(
    std::mem::offset_of!(EnvoyBufferPair, 0)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_envoy_http_header,
        key_ptr
      )
  );
  assert!(
    std::mem::offset_of!(EnvoyBufferPair, 1)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_envoy_http_header,
        value_ptr
      )
  );
};

#[cfg(test)]
mod tests {
  use super::*;

  #[test]
  fn test_envoy_buffer_as_slice_returns_underlying_bytes() {
    assert_eq!(EnvoyBuffer::new(b"hello").as_slice(), b"hello");
  }

  #[test]
  fn test_envoy_buffer_as_slice_treats_null_and_empty_alike() {
    // A null buffer and a non-null zero-length buffer both yield an empty slice, so the list getters
    // can return Envoy-filled entries directly without normalizing empty ones.
    assert_eq!(EnvoyBuffer::default().as_slice(), b"");
    assert_eq!(EnvoyBuffer::new(b"").as_slice(), b"");
  }

  #[test]
  fn test_envoy_mut_buffer_round_trips_through_slices() {
    let mut data = *b"hello";
    let mut buffer = unsafe { EnvoyMutBuffer::new_from_raw(data.as_mut_ptr(), data.len()) };
    assert_eq!(buffer.as_slice(), b"hello");
    buffer.as_mut_slice()[0] = b'j';
    assert_eq!(buffer.as_slice(), b"jello");
  }

  #[test]
  fn test_envoy_mut_buffer_slices_are_empty_when_null() {
    let mut buffer = EnvoyMutBuffer::default();
    assert_eq!(buffer.as_slice(), b"");
    assert_eq!(buffer.as_mut_slice(), b"");
  }
}
