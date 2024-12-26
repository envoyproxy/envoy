#[cfg(test)]
#[path = "./buffer_test.rs"]
mod buffer_test;

mod envoy_buffer {
  #[cfg(any(test, feature = "testing"))]
  use mockall::automock;

  /// A buffer struct that represents a contiguous memory region owned by Envoy.
  /// This has a mock implementation for testing.
  ///
  /// The life time (not in Rust sense) of the buffer is managed by Envoy, and it depends on how
  /// this [`EnvoyBuffer`] is created, for example, via [`crate::EnvoyHttpFilter`]'s methods.
  #[repr(C)]
  pub struct EnvoyBuffer {
    raw_ptr: *const u8,
    length: usize,
  }

  #[cfg_attr(any(test, feature = "testing"), automock)]
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
}

#[cfg(not(any(test, feature = "testing")))]
pub type EnvoyBuffer = envoy_buffer::EnvoyBuffer;
#[cfg(any(test, feature = "testing"))]
pub type EnvoyBuffer = envoy_buffer::MockEnvoyBuffer;
