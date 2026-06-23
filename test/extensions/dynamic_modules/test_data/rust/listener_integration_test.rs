use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_listener_filter_init_functions!(init, new_listener_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewListenerFilterConfigFunction`]
/// signature.
fn new_listener_filter_config_fn<EC: EnvoyListenerFilterConfig, ELF: EnvoyListenerFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn ListenerFilterConfig<ELF>>> {
  match name {
    "write_to_socket" => Some(Box::new(WriteToSocketFilterConfig)),
    "buffer_read" => Some(Box::new(BufferReadFilterConfig)),
    "http_callout_on_accept" => Some(Box::new(HttpCalloutOnAcceptFilterConfig)),
    "dynamic_metadata" => Some(Box::new(DynamicMetadataFilterConfig)),
    _ => panic!("unknown filter name: {name}"),
  }
}

// =============================================================================
// Write To Socket Test Filter
// =============================================================================

// Exercises connection-level callbacks on accept: connection start time, remote/local
// addresses, and the requested server name / detected transport protocol setters.
struct WriteToSocketFilterConfig;

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for WriteToSocketFilterConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(WriteToSocketFilter)
  }
}

struct WriteToSocketFilter;

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for WriteToSocketFilter {
  fn on_accept(
    &mut self,
    envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    assert!(envoy_filter.get_connection_start_time_ms() > 0);
    assert!(envoy_filter.get_remote_address().is_some());
    assert!(envoy_filter.get_local_address().is_some());
    envoy_filter.set_requested_server_name("sdk.listener.test");
    envoy_filter.set_detected_transport_protocol("sdk_listener");
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }
}

// =============================================================================
// Buffer Read Test Filter
// =============================================================================

// Stops iteration on accept and waits for data, then inspects the buffered bytes via
// get_buffer_chunk and verifies the configured max read bytes.
struct BufferReadFilterConfig;

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for BufferReadFilterConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(BufferReadFilter)
  }
}

struct BufferReadFilter;

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for BufferReadFilter {
  fn on_accept(
    &mut self,
    _envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
  }

  fn on_data(
    &mut self,
    envoy_filter: &mut ELF,
    data_length: usize,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    assert!(data_length >= 4);
    let chunk = envoy_filter
      .get_buffer_chunk()
      .expect("expected buffer chunk");
    assert_eq!(&chunk.as_slice()[..4], b"ping");
    assert_eq!(envoy_filter.max_read_bytes(), 4);
    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }

  fn max_read_bytes(&mut self, _envoy_filter: &mut ELF) -> usize {
    4
  }
}

// =============================================================================
// HTTP Callout On Accept Test Filter
// =============================================================================

// Issues an HTTP callout on accept and resumes the chain when it completes. The callout calls
// shared_from_this on the filter, which throws std::bad_weak_ptr and aborts the worker unless the
// production factory shares ownership. The callout targets cluster_0, served by the test autonomous
// upstream.
struct HttpCalloutOnAcceptFilterConfig;

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for HttpCalloutOnAcceptFilterConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(HttpCalloutOnAcceptFilter)
  }
}

struct HttpCalloutOnAcceptFilter;

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for HttpCalloutOnAcceptFilter {
  fn on_accept(
    &mut self,
    envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    let (result, _id) = envoy_filter.send_http_callout(
      "cluster_0",
      vec![
        (":method", b"GET"),
        (":path", b"/"),
        ("host", b"example.com"),
      ],
      None,
      5000,
    );
    // Always wait for the callout. The accept resumes only from on_http_callout_done, so the test
    // echo proves the round trip ran. The autonomous upstream makes the callout succeed.
    assert_eq!(
      result,
      abi::envoy_dynamic_module_type_http_callout_init_result::Success
    );
    abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
  }

  fn on_http_callout_done(
    &mut self,
    envoy_filter: &mut ELF,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Vec<(EnvoyBuffer, EnvoyBuffer)>,
    _response_body: Vec<EnvoyBuffer>,
  ) {
    envoy_filter.continue_filter_chain(true);
  }
}

// =============================================================================
// Dynamic Metadata Test Filter
// =============================================================================

// Sets several string entries under one namespace via the batch setter on accept, then reads them
// back to prove the batch and the getter round trip through the ABI. An empty batch must not create
// a namespace.
struct DynamicMetadataFilterConfig;

impl<ELF: EnvoyListenerFilter> ListenerFilterConfig<ELF> for DynamicMetadataFilterConfig {
  fn new_listener_filter(&self, _envoy: &mut ELF) -> Box<dyn ListenerFilter<ELF>> {
    Box::new(DynamicMetadataFilter)
  }
}

struct DynamicMetadataFilter;

impl<ELF: EnvoyListenerFilter> ListenerFilter<ELF> for DynamicMetadataFilter {
  fn on_accept(
    &mut self,
    envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    envoy_filter.set_dynamic_metadata_string_batch(
      "dynamic_modules.test",
      &[
        ("batch_key1", "batch_value1"),
        ("batch_key2", "batch_value2"),
      ],
    );
    envoy_filter.set_dynamic_metadata_string_batch("dynamic_modules.empty", &[]);

    assert_eq!(
      envoy_filter
        .get_dynamic_metadata_string("dynamic_modules.test", "batch_key1")
        .map(|value| value.as_slice().to_vec()),
      Some(b"batch_value1".to_vec())
    );
    assert_eq!(
      envoy_filter
        .get_dynamic_metadata_string("dynamic_modules.test", "batch_key2")
        .map(|value| value.as_slice().to_vec()),
      Some(b"batch_value2".to_vec())
    );
    assert!(envoy_filter
      .get_dynamic_metadata_string("dynamic_modules.empty", "batch_key1")
      .is_none());

    // Set a non-UTF-8 byte value and read it back to prove set_dynamic_metadata_bytes preserves it.
    envoy_filter.set_dynamic_metadata_bytes(
      "dynamic_modules.test",
      "bytes_key",
      &[0xff, 0x00, 0xfe],
    );
    assert_eq!(
      envoy_filter
        .get_dynamic_metadata_string("dynamic_modules.test", "bytes_key")
        .map(|value| value.as_slice().to_vec()),
      Some(vec![0xff, 0x00, 0xfe])
    );

    abi::envoy_dynamic_module_type_on_listener_filter_status::Continue
  }
}
