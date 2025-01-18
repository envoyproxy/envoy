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

/// This implements the [`std::io::Read`] for reading request body data.
pub struct RequestBodyReader<'a> {
  raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
  offset: usize,
}

impl std::io::Read for RequestBodyReader<'_> {
  fn read(&mut self, _buf: &mut [u8]) -> std::io::Result<usize> {
    let size = unsafe {
      envoy_dynamic_module_callback_http_read_request_body(
        self.raw_ptr,
        self.offset,
        _buf.as_ptr() as _,
        _buf.len(),
      )
    };
    self.offset += size;
    Result::Ok(size)
  }
}

impl RequestBodyReader<'_> {
  pub fn new(raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
      offset: 0,
    }
  }
}

/// This implements the [`std::io::Write`] for writing request body data.
pub struct RequestBodyWriter<'a> {
  raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
}

impl RequestBodyWriter<'_> {
  pub fn new(raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
    }
  }
}

impl std::io::Write for RequestBodyWriter<'_> {
  fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
    unsafe {
      envoy_dynamic_module_callback_http_write_request_body(
        self.raw_ptr,
        buf.as_ptr() as _,
        buf.len(),
      )
    };
    Result::Ok(buf.len())
  }

  fn flush(&mut self) -> std::io::Result<()> {
    Result::Ok(())
  }
}

/// This implements the [`std::io::Read`] for reading response body data.
pub struct ResponseBodyReader<'a> {
  raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
  offset: usize,
}

impl std::io::Read for ResponseBodyReader<'_> {
  fn read(&mut self, _buf: &mut [u8]) -> std::io::Result<usize> {
    let size = unsafe {
      envoy_dynamic_module_callback_http_read_response_body(
        self.raw_ptr,
        self.offset,
        _buf.as_ptr() as _,
        _buf.len(),
      )
    };
    self.offset += size;
    Result::Ok(size)
  }
}

/// This implements the [`std::io::Write`] for writing response body data.
impl ResponseBodyReader<'_> {
  pub fn new(raw_ptr: crate::abi::envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
      offset: 0,
    }
  }
}

pub struct ResponseBodyWriter<'a> {
  raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr,
  _marker: std::marker::PhantomData<&'a ()>,
}

impl ResponseBodyWriter<'_> {
  pub fn new(raw_ptr: envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self {
      raw_ptr,
      _marker: std::marker::PhantomData,
    }
  }
}

impl std::io::Write for ResponseBodyWriter<'_> {
  fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
    unsafe {
      envoy_dynamic_module_callback_http_write_response_body(
        self.raw_ptr,
        buf.as_ptr() as _,
        buf.len(),
      )
    };
    Result::Ok(buf.len())
  }

  fn flush(&mut self) -> std::io::Result<()> {
    Result::Ok(())
  }
}
