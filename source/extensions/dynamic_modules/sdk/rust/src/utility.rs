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

pub(crate) struct HeaderPairSlice(
  pub(crate) *const crate::abi::envoy_dynamic_module_type_module_http_header,
  pub(crate) usize,
);

const _: () = {
  type HeaderPair<'a> = (&'a str, &'a [u8]);
  assert!(
    std::mem::size_of::<HeaderPair>()
      == std::mem::size_of::<crate::abi::envoy_dynamic_module_type_module_http_header>()
  );
  assert!(
    std::mem::align_of::<HeaderPair>()
      == std::mem::align_of::<crate::abi::envoy_dynamic_module_type_module_http_header>()
  );

  assert!(
    std::mem::offset_of!(HeaderPair, 0)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_module_http_header,
        key_ptr
      )
  );
  assert!(
    std::mem::offset_of!(HeaderPair, 1)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_module_http_header,
        value_ptr
      )
  );
};

impl<'a> From<&[(&'a str, &'a [u8])]> for HeaderPairSlice {
  fn from(headers: &[(&'a str, &'a [u8])]) -> Self {
    // Note: Casting a (&str, &[u8]) to an abi::envoy_dynamic_module_type_module_http_header works
    // not because of any formal layout guarantees but because:
    // 1) tuples _in practice_ are laid out packed and in order
    // 2) &str and &[u8] are fat pointers (pointers to DSTs), whose layouts _in practice_ are a
    //    pointer and length
    // If these assumptions change, this will break, so we assert on them here in debug builds.
    type HeaderPair<'a> = (&'a str, &'a [u8]);

    debug_assert!({
      let pair: HeaderPair<'_> = ("test", b"value");
      let constructed = crate::abi::envoy_dynamic_module_type_module_http_header {
        key_ptr: pair.0.as_ptr() as *const _,
        key_length: pair.0.len(),
        value_ptr: pair.1.as_ptr() as *const _,
        value_length: pair.1.len(),
      };
      let punned = unsafe {
        std::mem::transmute::<HeaderPair, crate::abi::envoy_dynamic_module_type_module_http_header>(
          pair,
        )
      };
      constructed == punned
    });

    let ptr = headers.as_ptr() as *const crate::abi::envoy_dynamic_module_type_module_http_header;
    HeaderPairSlice(ptr, headers.len())
  }
}

#[cfg(test)]
#[allow(static_mut_refs)]
mod tests {
  use super::*;
  use crate::{EnvoyMutBuffer, MockEnvoyHttpFilter};

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
