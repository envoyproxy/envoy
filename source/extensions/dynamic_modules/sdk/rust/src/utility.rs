use crate::EnvoyHttpFilter;

fn get_body_content<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF, request: bool) -> Vec<u8> {
  // If the received body is the same as the buffered body (a previous filter did StopAndBuffer
  // and resumed), skip the received body to avoid duplicating data.
  let is_buffered = if request {
    envoy_filter.received_buffered_request_body()
  } else {
    envoy_filter.received_buffered_response_body()
  };

  let buffered_size = if request {
    envoy_filter.get_buffered_request_body_size()
  } else {
    envoy_filter.get_buffered_response_body_size()
  };

  let received_size = if is_buffered {
    0
  } else if request {
    envoy_filter.get_received_request_body_size()
  } else {
    envoy_filter.get_received_response_body_size()
  };

  let mut result = Vec::with_capacity(buffered_size + received_size);

  let buffered = if request {
    envoy_filter.get_buffered_request_body()
  } else {
    envoy_filter.get_buffered_response_body()
  };
  if let Some(chunks) = buffered {
    for chunk in &chunks {
      result.extend_from_slice(chunk.as_slice());
    }
  }

  if !is_buffered {
    let received = if request {
      envoy_filter.get_received_request_body()
    } else {
      envoy_filter.get_received_response_body()
    };
    if let Some(chunks) = received {
      for chunk in &chunks {
        result.extend_from_slice(chunk.as_slice());
      }
    }
  }

  result
}

/// Reads the whole request body by combining the buffered body and the latest received body.
/// This will copy all request body content into a module owned `Vec<u8>`.
///
/// This should only be called after we see the end of the request, which means the
/// `end_of_stream` flag is true in the `on_request_body` callback or we are in the
/// `on_request_trailers` callback.
pub fn read_whole_request_body<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF) -> Vec<u8> {
  get_body_content(envoy_filter, true)
}

/// Reads the whole response body by combining the buffered body and the latest received body.
/// This will copy all response body content into a module owned `Vec<u8>`.
///
/// This should only be called after we see the end of the response, which means the
/// `end_of_stream` flag is true in the `on_response_body` callback or we are in the
/// `on_response_trailers` callback.
pub fn read_whole_response_body<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF) -> Vec<u8> {
  get_body_content(envoy_filter, false)
}

/// Owned C-ABI array of HTTP header pairs materialised from a borrowed `&[(&str, &[u8])]`
/// for hand-off across the dynamic-modules FFI boundary.
///
/// Rust's `&str` and `&[u8]` fat-pointer layouts and tuple field order are formally
/// unspecified (see the Rust reference, "Type layout"). An earlier version of this type
/// pun-cast `&[(&str, &[u8])].as_ptr()` to `*const envoy_dynamic_module_type_module_http_header`
/// and relied on the in-practice `(ptr, len)` layout, with a `debug_assert!`-only transmute
/// check. A future rustc that rearranges those layouts would silently corrupt header data
/// in release builds. This implementation instead builds the FFI array field by field over
/// the `#[repr(C)]` C struct, which is sound by construction.
pub(crate) struct HeaderPairSlice<'a> {
  storage: Vec<crate::abi::envoy_dynamic_module_type_module_http_header>,
  _marker: std::marker::PhantomData<&'a [(&'a str, &'a [u8])]>,
}

impl HeaderPairSlice<'_> {
  pub(crate) fn as_ptr(&self) -> *const crate::abi::envoy_dynamic_module_type_module_http_header {
    self.storage.as_ptr()
  }

  pub(crate) fn len(&self) -> usize {
    self.storage.len()
  }

  // Companion to `len`; satisfies `clippy::len_without_is_empty`. The crate-level
  // `#![allow(dead_code)]` already covers the lack of internal callers.
  pub(crate) fn is_empty(&self) -> bool {
    self.storage.is_empty()
  }
}

impl<'a> From<&[(&'a str, &'a [u8])]> for HeaderPairSlice<'a> {
  fn from(headers: &[(&'a str, &'a [u8])]) -> Self {
    let storage = headers
      .iter()
      .map(
        |(k, v)| crate::abi::envoy_dynamic_module_type_module_http_header {
          key_ptr: k.as_ptr() as *const _,
          key_length: k.len(),
          value_ptr: v.as_ptr() as *const _,
          value_length: v.len(),
        },
      )
      .collect();
    Self {
      storage,
      _marker: std::marker::PhantomData,
    }
  }
}

#[cfg(test)]
#[allow(static_mut_refs)]
mod tests {
  use super::*;
  use crate::{EnvoyMutBuffer, MockEnvoyHttpFilter};

  #[test]
  fn test_header_pair_slice_populates_repr_c_struct_per_entry() {
    // Verifies that every (&str, &[u8]) entry produces a C struct whose four named fields
    // hold exactly the source pointers and lengths. Reads the storage through the C struct's
    // own field accessors (not by punning), so the test would fail if the conversion ever
    // got the field order wrong — which is the regression class the previous transmute-based
    // debug_assert was trying to detect at runtime in debug builds only.
    let pairs: &[(&str, &[u8])] = &[
      ("content-type", b"application/json"),
      (":status", b"200"),
      ("x-custom", b""),
    ];
    let slice = HeaderPairSlice::from(pairs);
    assert_eq!(slice.len(), 3);

    let materialised: &[crate::abi::envoy_dynamic_module_type_module_http_header] =
      unsafe { std::slice::from_raw_parts(slice.as_ptr(), slice.len()) };
    for (i, (k, v)) in pairs.iter().enumerate() {
      assert_eq!(materialised[i].key_ptr as *const u8, k.as_ptr());
      assert_eq!(materialised[i].key_length, k.len());
      assert_eq!(materialised[i].value_ptr as *const u8, v.as_ptr());
      assert_eq!(materialised[i].value_length, v.len());
    }
  }

  #[test]
  fn test_header_pair_slice_empty_input_produces_empty_storage() {
    let pairs: &[(&str, &[u8])] = &[];
    let slice = HeaderPairSlice::from(pairs);
    assert_eq!(slice.len(), 0);
  }

  #[test]
  fn test_read_whole_request_body_received_is_buffered() {
    // When received_buffered_request_body() returns true (previous filter did StopAndBuffer and
    // resumed), only the buffered body should be read to avoid duplicating data.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_request_body_size()
      .times(1)
      .returning(|| 11);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
    // get_received_request_body_size and get_received_request_body should NOT be called.

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_different_chunks() {
    // When received_buffered_request_body() returns false, both buffered and received are combined.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_buffered_request_body_size()
      .times(1)
      .returning(|| 6);
    mock
      .expect_get_received_request_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFERED) }]));
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_empty_buffered() {
    // When buffered body is empty (None) and not the same, result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_buffered_request_body_size()
      .times(1)
      .returning(|| 0);
    mock
      .expect_get_received_request_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| None);
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_request_body(&mut mock), b"world");
  }

  #[test]
  fn test_read_whole_response_body_received_is_buffered() {
    // When received_buffered_response_body() returns true, only the buffered body should be read.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_response_body_size()
      .times(1)
      .returning(|| 11);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
    // get_received_response_body_size and get_received_response_body should NOT be called.

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_different_chunks() {
    // When received_buffered_response_body() returns false, both buffered and received are
    // combined.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_buffered_response_body_size()
      .times(1)
      .returning(|| 6);
    mock
      .expect_get_received_response_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFERED) }]));
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_empty_buffered() {
    // When buffered body is empty (None) and not the same, result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_buffered_response_body_size()
      .times(1)
      .returning(|| 0);
    mock
      .expect_get_received_response_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| None);
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_response_body(&mut mock), b"world");
  }
}
