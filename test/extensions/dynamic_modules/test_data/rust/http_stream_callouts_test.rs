use abi::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_init_functions!(
  init,
  new_http_filter_config_fn,
  new_http_filter_per_route_config_fn
);

fn init() -> bool {
  true
}

fn new_http_filter_config_fn<EC: EnvoyHttpFilterConfig, EHF: EnvoyHttpFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HttpFilterConfig<EHF>>> {
  match name {
    "basic_stream_lifecycle" => Some(Box::new(BasicStreamLifecycleConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "bidirectional_streaming" => Some(Box::new(BidirectionalStreamingConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "multiple_streams" => Some(Box::new(MultipleStreamsConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "stream_reset" => Some(Box::new(StreamResetConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    "upstream_reset" => Some(Box::new(UpstreamResetConfig {
      cluster_name: String::from_utf8(config.to_owned()).unwrap(),
    })),
    _ => panic!("Unknown filter name: {}", name),
  }
}

fn new_http_filter_per_route_config_fn(
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn std::any::Any>> {
  None
}

// =============================================================================
// Test 1: Basic Stream Lifecycle
// Tests: Start stream, receive headers/data, stream completes successfully.
// =============================================================================

struct BasicStreamLifecycleConfig {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for BasicStreamLifecycleConfig {
  fn new_http_filter(&mut self, _envoy_filter_config: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(BasicStreamLifecycleFilter {
      cluster_name: self.cluster_name.clone(),
      received_response: false,
    })
  }
}

struct BasicStreamLifecycleFilter {
  cluster_name: String,
  received_response: bool,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for BasicStreamLifecycleFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Start an HTTP stream.
    let (result, handle) = envoy_filter.start_http_stream(
      &self.cluster_name,
      vec![
        (":path", b"/test"),
        (":method", b"GET"),
        ("host", b"example.com"),
      ],
      None, // No body.
      true, // end_stream = true.
      5000, // 5 second timeout.
    );

    if result != envoy_dynamic_module_type_http_callout_init_result::Success {
      envoy_filter.send_response(500, vec![("x-error", b"stream_init_failed")], None, None);
      return envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
    }

    // Store handle if needed (not storing in this simple test).
    let _ = handle;

    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_stream_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _headers: &[(EnvoyBuffer, EnvoyBuffer)],
    _end_stream: bool,
  ) {
    // Process headers.
  }

  fn on_http_stream_data(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _data: &[EnvoyBuffer],
    _end_stream: bool,
  ) {
    // Process data.
  }

  fn on_http_stream_complete(
    &mut self,
    envoy_filter: &mut EHF,
    _stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
  ) {
    self.received_response = true;
    envoy_filter.send_response(
      200,
      vec![("x-stream", b"success")],
      Some(b"stream_callout_success"),
      None,
    );
  }
}

impl Drop for BasicStreamLifecycleFilter {
  fn drop(&mut self) {
    // Ensure we received the response.
    assert!(
      self.received_response,
      "Stream did not complete successfully"
    );
  }
}

// =============================================================================
// Test 2: Bidirectional Streaming
// Tests: Start stream, send data chunks, receive data chunks, send trailers.
// =============================================================================

struct BidirectionalStreamingConfig {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for BidirectionalStreamingConfig {
  fn new_http_filter(&mut self, _envoy_filter_config: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(BidirectionalStreamingFilter {
      cluster_name: self.cluster_name.clone(),
      stream_handle: std::ptr::null_mut(),
      chunks_received: 0,
    })
  }
}

struct BidirectionalStreamingFilter {
  cluster_name: String,
  stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
  chunks_received: usize,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for BidirectionalStreamingFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Start an HTTP stream with POST method.
    let (result, handle) = envoy_filter.start_http_stream(
      &self.cluster_name,
      vec![
        (":path", b"/stream"),
        (":method", b"POST"),
        ("host", b"example.com"),
        ("content-type", b"application/octet-stream"),
      ],
      None,  // No initial body - we'll stream it.
      false, // end_stream = false.
      10000,
    );

    if result != envoy_dynamic_module_type_http_callout_init_result::Success {
      envoy_filter.send_response(500, vec![("x-error", b"stream_init_failed")], None, None);
      return envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
    }

    self.stream_handle = handle;

    // Send chunk 1.
    let chunk1 = b"chunk1";
    let success = unsafe { envoy_filter.send_http_stream_data(handle, chunk1, false) };
    assert!(success);

    // Send chunk 2.
    let chunk2 = b"chunk2";
    let success = unsafe { envoy_filter.send_http_stream_data(handle, chunk2, false) };
    assert!(success);

    // Send trailers to end the stream.
    let trailers = vec![("x-trailer", b"value" as &[u8])];
    let success = unsafe { envoy_filter.send_http_stream_trailers(handle, trailers) };
    assert!(success);

    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_stream_data(
    &mut self,
    _envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _data: &[EnvoyBuffer],
    _end_stream: bool,
  ) {
    assert_eq!(stream_handle, self.stream_handle);
    self.chunks_received += 1;
  }

  fn on_http_stream_complete(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
  ) {
    assert_eq!(stream_handle, self.stream_handle);
    let chunks_str = self.chunks_received.to_string();
    envoy_filter.send_response(
      200,
      vec![("x-chunks-received", chunks_str.as_bytes())],
      Some(b"bidirectional_success"),
      None,
    );
  }
}

// =============================================================================
// Test 3: Multiple Streams
// Tests: Start multiple streams concurrently.
// =============================================================================

struct MultipleStreamsConfig {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for MultipleStreamsConfig {
  fn new_http_filter(&mut self, _envoy_filter_config: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(MultipleStreamsFilter {
      cluster_name: self.cluster_name.clone(),
      stream_handles: Vec::new(),
      completed_streams: 0,
    })
  }
}

struct MultipleStreamsFilter {
  cluster_name: String,
  stream_handles: Vec<envoy_dynamic_module_type_http_stream_envoy_ptr>,
  completed_streams: usize,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for MultipleStreamsFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Create 3 concurrent streams.
    for i in 1 ..= 3 {
      let path = format!("/stream{}", i);
      let (result, handle) = envoy_filter.start_http_stream(
        &self.cluster_name,
        vec![
          (":path", path.as_bytes()),
          (":method", b"GET"),
          ("host", b"example.com"),
        ],
        None,
        true, // end_stream = true.
        5000,
      );

      if result == envoy_dynamic_module_type_http_callout_init_result::Success {
        self.stream_handles.push(handle);
      }
    }

    if self.stream_handles.len() != 3 {
      envoy_filter.send_response(500, vec![("x-error", b"stream_init_failed")], None, None);
    }

    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_stream_complete(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
  ) {
    assert!(self.stream_handles.contains(&stream_handle));
    self.completed_streams += 1;

    if self.completed_streams == 3 {
      envoy_filter.send_response(200, vec![("x-stream", b"all_success")], None, None);
    }
  }
}

// =============================================================================
// Test 4: Stream Reset
// Tests: Reset an ongoing stream explicitly.
// =============================================================================

struct StreamResetConfig {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for StreamResetConfig {
  fn new_http_filter(&mut self, _envoy_filter_config: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(StreamResetFilter {
      cluster_name: self.cluster_name.clone(),
      stream_handle: std::ptr::null_mut(),
      received_headers: false,
      reset_called: false,
    })
  }
}

struct StreamResetFilter {
  cluster_name: String,
  stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
  received_headers: bool,
  reset_called: bool,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for StreamResetFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Start a stream to a cluster that will be reset.
    let (result, handle) = envoy_filter.start_http_stream(
      &self.cluster_name,
      vec![
        (":path", b"/slow"),
        (":method", b"GET"),
        ("host", b"example.com"),
      ],
      None,
      true, // end_stream = true.
      5000,
    );

    if result != envoy_dynamic_module_type_http_callout_init_result::Success {
      envoy_filter.send_response(500, vec![("x-error", b"stream_init_failed")], None, None);
      return envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
    }

    self.stream_handle = handle;
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_stream_headers(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _headers: &[(EnvoyBuffer, EnvoyBuffer)],
    _end_stream: bool,
  ) {
    assert_eq!(stream_handle, self.stream_handle);
    self.received_headers = true;

    // Immediately reset the stream after receiving headers.
    unsafe {
      envoy_filter.reset_http_stream(stream_handle);
    }
  }

  fn on_http_stream_reset(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _reset_reason: envoy_dynamic_module_type_http_stream_reset_reason,
  ) {
    assert_eq!(stream_handle, self.stream_handle);
    self.reset_called = true;

    // Send response indicating reset occurred.
    envoy_filter.send_response(
      200,
      vec![("x-stream", b"reset_ok")],
      Some(b"stream_was_reset"),
      None,
    );
  }
}

impl Drop for StreamResetFilter {
  fn drop(&mut self) {
    assert!(self.received_headers, "Never received headers before reset");
    assert!(self.reset_called, "Reset callback was not called");
  }
}

// =============================================================================
// Test 5: Upstream Reset
// Tests: Start stream, upstream resets connection, receive reset callback.
// =============================================================================

struct UpstreamResetConfig {
  cluster_name: String,
}

impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for UpstreamResetConfig {
  fn new_http_filter(&mut self, _envoy_filter_config: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    Box::new(UpstreamResetFilter {
      cluster_name: self.cluster_name.clone(),
      stream_handle: std::ptr::null_mut(),
    })
  }
}

struct UpstreamResetFilter {
  cluster_name: String,
  stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
}

impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for UpstreamResetFilter {
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
    // Start a stream that we expect to be reset by the upstream.
    let (result, handle) = envoy_filter.start_http_stream(
      &self.cluster_name,
      vec![
        (":path", b"/reset"),
        (":method", b"GET"),
        ("host", b"example.com"),
      ],
      None,
      true,
      5000,
    );

    if result != envoy_dynamic_module_type_http_callout_init_result::Success {
      envoy_filter.send_response(500, vec![("x-error", b"stream_init_failed")], None, None);
      return envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration;
    }

    self.stream_handle = handle;
    envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  }

  fn on_http_stream_reset(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: envoy_dynamic_module_type_http_stream_envoy_ptr,
    _reason: envoy_dynamic_module_type_http_stream_reset_reason,
  ) {
    assert_eq!(stream_handle, self.stream_handle);
    envoy_filter.send_response(
      200,
      vec![("x-reset", b"true")],
      Some(b"upstream_reset"),
      None,
    );
  }
}
