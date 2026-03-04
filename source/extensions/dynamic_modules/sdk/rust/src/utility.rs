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

#[cfg(test)]
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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFERED })]));
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));

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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));

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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFERED })]));
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));

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
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));

    assert_eq!(read_whole_response_body(&mut mock), b"world");
  }
}
