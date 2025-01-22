use crate::abi::*;

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

type ReadHttpBodyFn = unsafe extern "C" fn(
  raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  offset: usize,
  buf: envoy_dynamic_module_type_buffer_module_ptr,
  buf_len: usize,
  size: *mut usize,
) -> bool;

type WriteHttpBodyFn = unsafe extern "C" fn(
  raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  buf: envoy_dynamic_module_type_buffer_module_ptr,
  buf_len: usize,
  size: *mut usize,
) -> bool;

/// This implements the [`std::io::Read`] for reading http request body data.
pub struct HttpBodyReader<'a> {
  raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
  offset: usize,
  read_fn: ReadHttpBodyFn,
}

impl std::io::Read for HttpBodyReader<'_> {
  fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
    let mut size: usize = 0;
    let ok = unsafe {
      (self.read_fn)(
        self.raw_ptr,
        self.offset,
        buf.as_ptr() as _,
        buf.len(),
        &mut size,
      )
    };
    if !ok {
      return Result::Err(std::io::Error::new(
        std::io::ErrorKind::Other,
        "Request body not available",
      ));
    }
    self.offset += size;
    Result::Ok(size)
  }
}

impl HttpBodyReader<'_> {
  pub fn new(
    raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr,
    read_fn: ReadHttpBodyFn,
  ) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
      offset: 0,
      read_fn,
    }
  }
}

/// This implements the [`std::io::Write`] for writing http request body data.
pub struct HttpBodyWriter<'a> {
  raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
  write_fn: WriteHttpBodyFn,
}

impl HttpBodyWriter<'_> {
  pub fn new(
    raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr,
    write_fn: WriteHttpBodyFn,
  ) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
      write_fn,
    }
  }
}

impl std::io::Write for HttpBodyWriter<'_> {
  fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
    let mut size: usize = 0;
    let ok = unsafe { (self.write_fn)(self.raw_ptr, buf.as_ptr() as _, buf.len(), &mut size) };
    if !ok {
      return Result::Err(std::io::Error::new(
        std::io::ErrorKind::Other,
        "Request body not available",
      ));
    }
    Result::Ok(size)
  }

  fn flush(&mut self) -> std::io::Result<()> {
    Result::Ok(())
  }
}
