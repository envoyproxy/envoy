use crate::abi;

/// Timing information from the stream info.
#[derive(Debug, Clone, Default)]
pub struct TimingInfo {
  /// Request start time as Unix timestamp in nanoseconds.
  pub start_time_unix_ns: i64,
  /// Duration from start to request complete in nanoseconds, or -1 if not available.
  pub request_complete_duration_ns: i64,
  /// Time of first upstream TX byte sent in nanoseconds, or -1 if not available.
  pub first_upstream_tx_byte_sent_ns: i64,
  /// Time of last upstream TX byte sent in nanoseconds, or -1 if not available.
  pub last_upstream_tx_byte_sent_ns: i64,
  /// Time of first upstream RX byte received in nanoseconds, or -1 if not available.
  pub first_upstream_rx_byte_received_ns: i64,
  /// Time of last upstream RX byte received in nanoseconds, or -1 if not available.
  pub last_upstream_rx_byte_received_ns: i64,
  /// Time of first downstream TX byte sent in nanoseconds, or -1 if not available.
  pub first_downstream_tx_byte_sent_ns: i64,
  /// Time of last downstream TX byte sent in nanoseconds, or -1 if not available.
  pub last_downstream_tx_byte_sent_ns: i64,
}

impl From<abi::envoy_dynamic_module_type_timing_info> for TimingInfo {
  fn from(info: abi::envoy_dynamic_module_type_timing_info) -> Self {
    let abi::envoy_dynamic_module_type_timing_info {
      start_time_unix_ns,
      request_complete_duration_ns,
      first_upstream_tx_byte_sent_ns,
      last_upstream_tx_byte_sent_ns,
      first_upstream_rx_byte_received_ns,
      last_upstream_rx_byte_received_ns,
      first_downstream_tx_byte_sent_ns,
      last_downstream_tx_byte_sent_ns,
    } = info;
    Self {
      start_time_unix_ns,
      request_complete_duration_ns,
      first_upstream_tx_byte_sent_ns,
      last_upstream_tx_byte_sent_ns,
      first_upstream_rx_byte_received_ns,
      last_upstream_rx_byte_received_ns,
      first_downstream_tx_byte_sent_ns,
      last_downstream_tx_byte_sent_ns,
    }
  }
}

pub(crate) fn unavailable_timing_info() -> abi::envoy_dynamic_module_type_timing_info {
  abi::envoy_dynamic_module_type_timing_info {
    start_time_unix_ns: -1,
    request_complete_duration_ns: -1,
    first_upstream_tx_byte_sent_ns: -1,
    last_upstream_tx_byte_sent_ns: -1,
    first_upstream_rx_byte_received_ns: -1,
    last_upstream_rx_byte_received_ns: -1,
    first_downstream_tx_byte_sent_ns: -1,
    last_downstream_tx_byte_sent_ns: -1,
  }
}
