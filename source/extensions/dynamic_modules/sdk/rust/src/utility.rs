use crate::EnvoyHttpFilter;

/// Collects raw pointer addresses from body chunks without copying data.
fn collect_chunk_ptrs<EHF: EnvoyHttpFilter>(
  envoy_filter: &mut EHF,
  request: bool,
  buffered: bool,
) -> Vec<usize> {
  let chunks = match (request, buffered) {
    (true, true) => envoy_filter.get_buffered_request_body(),
    (true, false) => envoy_filter.get_received_request_body(),
    (false, true) => envoy_filter.get_buffered_response_body(),
    (false, false) => envoy_filter.get_received_response_body(),
  };
  chunks.map_or(Vec::new(), |chunks| {
    chunks
      .iter()
      .map(|c| c.as_slice().as_ptr() as usize)
      .collect()
  })
}

/// Copies data from body chunks into a `Vec<u8>`.
fn collect_chunk_data<EHF: EnvoyHttpFilter>(
  envoy_filter: &mut EHF,
  request: bool,
  buffered: bool,
) -> Vec<u8> {
  let chunks = match (request, buffered) {
    (true, true) => envoy_filter.get_buffered_request_body(),
    (true, false) => envoy_filter.get_received_request_body(),
    (false, true) => envoy_filter.get_buffered_response_body(),
    (false, false) => envoy_filter.get_received_response_body(),
  };
  chunks.map_or(Vec::new(), |chunks| {
    let mut data = Vec::new();
    for chunk in &chunks {
      data.extend_from_slice(chunk.as_slice());
    }
    data
  })
}

fn get_body_content<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF, request: bool) -> Vec<u8> {
  // First, collect only raw pointers from both bodies to check for sameness, without copying
  // any data. Because of the complex buffering logic in Envoy, the received body may be the
  // same as the buffered body. This happens when a previous filter returns StopAndBuffer, and
  // then this filter is called again with the buffered body as the received body.
  // TODO(wbpcode): optimize this by adding a new ABI to directly check it.
  let received_ptrs = collect_chunk_ptrs(envoy_filter, request, false);
  let buffered_ptrs = collect_chunk_ptrs(envoy_filter, request, true);

  if received_ptrs == buffered_ptrs {
    // Same underlying memory: copy buffered data once.
    collect_chunk_data(envoy_filter, request, true)
  } else {
    // Different chunks: copy buffered first, then append received.
    let mut result = collect_chunk_data(envoy_filter, request, true);
    result.extend(collect_chunk_data(envoy_filter, request, false));
    result
  }
}

/// Reads the whole request body by combining the buffered body and the latest received body.
///
/// This should only be called after we see the end of the request, which means the
/// `end_of_stream` flag is true in the `on_request_body` callback or we are in the
/// `on_request_trailers` callback.
pub fn read_whole_request_body<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF) -> Vec<u8> {
  get_body_content(envoy_filter, true)
}

/// Reads the whole response body by combining the buffered body and the latest received body.
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
  fn test_read_whole_request_body_same_chunks() {
    // When received and buffered body point to the same underlying memory (which happens
    // when a previous filter returned StopAndBuffer), the data should appear only once.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    // get_received_request_body: called once (pointer collection).
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
    // get_buffered_request_body: called twice (pointer collection + data copy).
    mock
      .expect_get_buffered_request_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_different_chunks() {
    // When received and buffered bodies are different, the result is buffered + received.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    // Each getter is called twice: once for pointer collection, once for data copy.
    mock
      .expect_get_received_request_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));
    mock
      .expect_get_buffered_request_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFERED })]));

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_empty_buffered() {
    // When buffered body is empty (None), result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_get_received_request_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));
    mock
      .expect_get_buffered_request_body()
      .times(2)
      .returning(|| None);

    assert_eq!(read_whole_request_body(&mut mock), b"world");
  }

  #[test]
  fn test_read_whole_response_body_same_chunks() {
    // When received and buffered body point to the same underlying memory,
    // the data should appear only once.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));
    mock
      .expect_get_buffered_response_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFER })]));

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_different_chunks() {
    // When received and buffered bodies are different, the result is buffered + received.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_get_received_response_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));
    mock
      .expect_get_buffered_response_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut BUFFERED })]));

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_empty_buffered() {
    // When buffered body is empty (None), result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_get_received_response_body()
      .times(2)
      .returning(|| Some(vec![EnvoyMutBuffer::new(unsafe { &mut RECEIVED })]));
    mock
      .expect_get_buffered_response_body()
      .times(2)
      .returning(|| None);

    assert_eq!(read_whole_response_body(&mut mock), b"world");
  }
}
