use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

declare_network_filter_init_functions!(init, new_network_filter_config_fn);

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::ProgramInitFunction`] signature.
fn init() -> bool {
  true
}

/// This implements the [`envoy_proxy_dynamic_modules_rust_sdk::NewNetworkFilterConfigFunction`]
/// signature.
fn new_network_filter_config_fn<EC: EnvoyNetworkFilterConfig, ENF: EnvoyNetworkFilter>(
  _envoy_filter_config: &mut EC,
  name: &str,
  _config: &[u8],
) -> Option<Box<dyn NetworkFilterConfig<ENF>>> {
  match name {
    "flow_control" => Some(Box::new(FlowControlFilterConfig)),
    "connection_state" => Some(Box::new(ConnectionStateFilterConfig)),
    "half_close" => Some(Box::new(HalfCloseFilterConfig)),
    "buffer_limits" => Some(Box::new(BufferLimitsFilterConfig)),
    _ => panic!("unknown filter name: {}", name),
  }
}

// =============================================================================
// Flow Control Test Filter
// =============================================================================

// Tests read_disable/read_enabled for implementing back-pressure.
// On the first read, the filter disables reads, verifies the state transition,
// then re-enables reads and verifies the connection is readable again.
struct FlowControlFilterConfig;

impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for FlowControlFilterConfig {
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
    Box::new(FlowControlFilter {
      reads_disabled: false,
    })
  }
}

struct FlowControlFilter {
  reads_disabled: bool,
}

impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for FlowControlFilter {
  fn on_new_connection(
    &mut self,
    _envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_read(
    &mut self,
    envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    if !self.reads_disabled {
      // Disable reads to apply back-pressure.
      let status = envoy_filter.read_disable(true);
      assert_eq!(
        status,
        abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadDisabled
      );
      assert!(!envoy_filter.read_enabled());
      self.reads_disabled = true;

      // Re-enable reads immediately so the connection can proceed.
      let status = envoy_filter.read_disable(false);
      assert_eq!(
        status,
        abi::envoy_dynamic_module_type_network_read_disable_status::TransitionedToReadEnabled
      );
      assert!(envoy_filter.read_enabled());
    }
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_write(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
  }
}

// =============================================================================
// Connection State Test Filter
// =============================================================================

// Tests get_connection_state by verifying the connection is Open during data processing.
struct ConnectionStateFilterConfig;

impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for ConnectionStateFilterConfig {
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
    Box::new(ConnectionStateFilter)
  }
}

struct ConnectionStateFilter;

impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for ConnectionStateFilter {
  fn on_new_connection(
    &mut self,
    envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Connection should be open when new_connection is called.
    assert_eq!(
      envoy_filter.get_connection_state(),
      abi::envoy_dynamic_module_type_network_connection_state::Open
    );
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_read(
    &mut self,
    envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Connection should still be open during reads.
    assert_eq!(
      envoy_filter.get_connection_state(),
      abi::envoy_dynamic_module_type_network_connection_state::Open
    );
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_write(
    &mut self,
    envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Connection should still be open during writes.
    assert_eq!(
      envoy_filter.get_connection_state(),
      abi::envoy_dynamic_module_type_network_connection_state::Open
    );
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
  }
}

// =============================================================================
// Half-Close Test Filter
// =============================================================================

// Tests enable_half_close/is_half_close_enabled by enabling half-close semantics on the
// connection.
struct HalfCloseFilterConfig;

impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for HalfCloseFilterConfig {
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
    Box::new(HalfCloseFilter)
  }
}

struct HalfCloseFilter;

impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for HalfCloseFilter {
  fn on_new_connection(
    &mut self,
    envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Verify half-close is already enabled by the TCP proxy filter in the chain.
    assert!(envoy_filter.is_half_close_enabled());
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_read(
    &mut self,
    envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Test toggling half-close: disable then re-enable.
    envoy_filter.enable_half_close(false);
    assert!(!envoy_filter.is_half_close_enabled());
    envoy_filter.enable_half_close(true);
    assert!(envoy_filter.is_half_close_enabled());
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_write(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
  }
}

// =============================================================================
// Buffer Limits Test Filter
// =============================================================================

static ABOVE_HIGH_WATERMARK_CALLED: AtomicBool = AtomicBool::new(false);
static BELOW_LOW_WATERMARK_CALLED: AtomicBool = AtomicBool::new(false);
static INITIAL_BUFFER_LIMIT: AtomicU32 = AtomicU32::new(0);

#[no_mangle]
pub extern "C" fn getAboveHighWatermarkCalled() -> bool {
  ABOVE_HIGH_WATERMARK_CALLED.load(Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn getBelowLowWatermarkCalled() -> bool {
  BELOW_LOW_WATERMARK_CALLED.load(Ordering::SeqCst)
}

#[no_mangle]
pub extern "C" fn getInitialBufferLimit() -> u32 {
  INITIAL_BUFFER_LIMIT.load(Ordering::SeqCst)
}

// Tests get_buffer_limit/set_buffer_limits/above_high_watermark and watermark callbacks.
struct BufferLimitsFilterConfig;

impl<ENF: EnvoyNetworkFilter> NetworkFilterConfig<ENF> for BufferLimitsFilterConfig {
  fn new_network_filter(&self, _envoy: &mut ENF) -> Box<dyn NetworkFilter<ENF>> {
    ABOVE_HIGH_WATERMARK_CALLED.store(false, Ordering::SeqCst);
    BELOW_LOW_WATERMARK_CALLED.store(false, Ordering::SeqCst);
    Box::new(BufferLimitsFilter)
  }
}

struct BufferLimitsFilter;

impl<ENF: EnvoyNetworkFilter> NetworkFilter<ENF> for BufferLimitsFilter {
  fn on_new_connection(
    &mut self,
    envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    // Record initial buffer limit.
    let limit = envoy_filter.get_buffer_limit();
    INITIAL_BUFFER_LIMIT.store(limit, Ordering::SeqCst);

    // Set a new buffer limit.
    envoy_filter.set_buffer_limits(32768);
    assert_eq!(envoy_filter.get_buffer_limit(), 32768);

    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_read(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_write(
    &mut self,
    _envoy_filter: &mut ENF,
    _data_length: usize,
    _end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    abi::envoy_dynamic_module_type_on_network_filter_data_status::Continue
  }

  fn on_event(
    &mut self,
    _envoy_filter: &mut ENF,
    _event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
  }

  fn on_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut ENF) {
    ABOVE_HIGH_WATERMARK_CALLED.store(true, Ordering::SeqCst);
  }

  fn on_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut ENF) {
    BELOW_LOW_WATERMARK_CALLED.store(true, Ordering::SeqCst);
  }
}
