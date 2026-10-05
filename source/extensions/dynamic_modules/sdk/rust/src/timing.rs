use crate::abi;

/// Stream timing from Envoy. `start_time_unix_ns` is a Unix timestamp in nanoseconds; all other
/// fields are nanosecond offsets from request start. Each field is `None` when unavailable; zero is
/// valid. Connection and handshake offsets can be negative when the connection predates the request.
#[derive(Debug, Clone)]
pub struct TimingInfo {
  /// Request start time as Unix timestamp in nanoseconds.
  pub start_time_unix_ns: Option<i64>,
  /// Downstream connection acceptance offset in nanoseconds, when available.
  pub downstream_connection_begin_ns: Option<i64>,
  /// Downstream TLS ClientHello offset in nanoseconds, when available.
  pub downstream_handshake_start_ns: Option<i64>,
  /// Downstream TLS handshake completion offset in nanoseconds, when available.
  pub downstream_handshake_complete_ns: Option<i64>,
  /// Last downstream request header byte offset in nanoseconds, when available.
  pub last_downstream_header_rx_byte_received_ns: Option<i64>,
  /// Last downstream request byte offset in nanoseconds, when available.
  pub last_downstream_rx_byte_received_ns: Option<i64>,
  /// Upstream connection establishment start offset in nanoseconds, when available.
  pub upstream_connect_start_ns: Option<i64>,
  /// Upstream connection establishment completion offset in nanoseconds, when available.
  pub upstream_connect_complete_ns: Option<i64>,
  /// Upstream TLS handshake completion offset in nanoseconds, when available.
  pub upstream_handshake_complete_ns: Option<i64>,
  /// First upstream TX byte offset in nanoseconds, when available.
  pub first_upstream_tx_byte_sent_ns: Option<i64>,
  /// Last upstream TX byte offset in nanoseconds, when available.
  pub last_upstream_tx_byte_sent_ns: Option<i64>,
  /// First upstream RX byte offset in nanoseconds, when available.
  pub first_upstream_rx_byte_received_ns: Option<i64>,
  /// First upstream response body byte offset in nanoseconds, when available.
  pub first_upstream_rx_body_byte_received_ns: Option<i64>,
  /// Last upstream RX byte offset in nanoseconds, when available.
  pub last_upstream_rx_byte_received_ns: Option<i64>,
  /// First downstream TX byte offset in nanoseconds, when available.
  pub first_downstream_tx_byte_sent_ns: Option<i64>,
  /// Last downstream TX byte offset in nanoseconds, when available.
  pub last_downstream_tx_byte_sent_ns: Option<i64>,
  /// Final downstream ACK offset in nanoseconds, when available.
  pub last_downstream_ack_received_ns: Option<i64>,
  /// Duration from start to request complete in nanoseconds, when available.
  pub request_complete_duration_ns: Option<i64>,
  /// Downstream connection close offset in nanoseconds, when observed during this request.
  pub downstream_connection_end_ns: Option<i64>,
}

impl Default for TimingInfo {
  fn default() -> Self {
    unavailable_timing_info().into()
  }
}

impl From<abi::envoy_dynamic_module_type_timing_info_v2> for TimingInfo {
  fn from(info: abi::envoy_dynamic_module_type_timing_info_v2) -> Self {
    let abi::envoy_dynamic_module_type_timing_info_v2 {
      start_time_unix_ns,
      downstream_connection_begin_ns,
      downstream_handshake_start_ns,
      downstream_handshake_complete_ns,
      last_downstream_header_rx_byte_received_ns,
      last_downstream_rx_byte_received_ns,
      upstream_connect_start_ns,
      upstream_connect_complete_ns,
      upstream_handshake_complete_ns,
      first_upstream_tx_byte_sent_ns,
      last_upstream_tx_byte_sent_ns,
      first_upstream_rx_byte_received_ns,
      first_upstream_rx_body_byte_received_ns,
      last_upstream_rx_byte_received_ns,
      first_downstream_tx_byte_sent_ns,
      last_downstream_tx_byte_sent_ns,
      last_downstream_ack_received_ns,
      request_complete_duration_ns,
      downstream_connection_end_ns,
      has_start_time,
      has_downstream_connection_begin,
      has_downstream_handshake_start,
      has_downstream_handshake_complete,
      has_last_downstream_header_rx_byte_received,
      has_last_downstream_rx_byte_received,
      has_upstream_connect_start,
      has_upstream_connect_complete,
      has_upstream_handshake_complete,
      has_first_upstream_tx_byte_sent,
      has_last_upstream_tx_byte_sent,
      has_first_upstream_rx_byte_received,
      has_first_upstream_rx_body_byte_received,
      has_last_upstream_rx_byte_received,
      has_first_downstream_tx_byte_sent,
      has_last_downstream_tx_byte_sent,
      has_last_downstream_ack_received,
      has_request_complete,
      has_downstream_connection_end,
    } = info;
    Self {
      start_time_unix_ns: has_start_time.then_some(start_time_unix_ns),
      downstream_connection_begin_ns: has_downstream_connection_begin
        .then_some(downstream_connection_begin_ns),
      downstream_handshake_start_ns: has_downstream_handshake_start
        .then_some(downstream_handshake_start_ns),
      downstream_handshake_complete_ns: has_downstream_handshake_complete
        .then_some(downstream_handshake_complete_ns),
      last_downstream_header_rx_byte_received_ns: has_last_downstream_header_rx_byte_received
        .then_some(last_downstream_header_rx_byte_received_ns),
      last_downstream_rx_byte_received_ns: has_last_downstream_rx_byte_received
        .then_some(last_downstream_rx_byte_received_ns),
      upstream_connect_start_ns: has_upstream_connect_start.then_some(upstream_connect_start_ns),
      upstream_connect_complete_ns: has_upstream_connect_complete
        .then_some(upstream_connect_complete_ns),
      upstream_handshake_complete_ns: has_upstream_handshake_complete
        .then_some(upstream_handshake_complete_ns),
      first_upstream_tx_byte_sent_ns: has_first_upstream_tx_byte_sent
        .then_some(first_upstream_tx_byte_sent_ns),
      last_upstream_tx_byte_sent_ns: has_last_upstream_tx_byte_sent
        .then_some(last_upstream_tx_byte_sent_ns),
      first_upstream_rx_byte_received_ns: has_first_upstream_rx_byte_received
        .then_some(first_upstream_rx_byte_received_ns),
      first_upstream_rx_body_byte_received_ns: has_first_upstream_rx_body_byte_received
        .then_some(first_upstream_rx_body_byte_received_ns),
      last_upstream_rx_byte_received_ns: has_last_upstream_rx_byte_received
        .then_some(last_upstream_rx_byte_received_ns),
      first_downstream_tx_byte_sent_ns: has_first_downstream_tx_byte_sent
        .then_some(first_downstream_tx_byte_sent_ns),
      last_downstream_tx_byte_sent_ns: has_last_downstream_tx_byte_sent
        .then_some(last_downstream_tx_byte_sent_ns),
      last_downstream_ack_received_ns: has_last_downstream_ack_received
        .then_some(last_downstream_ack_received_ns),
      request_complete_duration_ns: has_request_complete.then_some(request_complete_duration_ns),
      downstream_connection_end_ns: has_downstream_connection_end
        .then_some(downstream_connection_end_ns),
    }
  }
}

pub(crate) fn unavailable_timing_info() -> abi::envoy_dynamic_module_type_timing_info_v2 {
  abi::envoy_dynamic_module_type_timing_info_v2 {
    start_time_unix_ns: -1,
    downstream_connection_begin_ns: -1,
    downstream_handshake_start_ns: -1,
    downstream_handshake_complete_ns: -1,
    last_downstream_header_rx_byte_received_ns: -1,
    last_downstream_rx_byte_received_ns: -1,
    upstream_connect_start_ns: -1,
    upstream_connect_complete_ns: -1,
    upstream_handshake_complete_ns: -1,
    first_upstream_tx_byte_sent_ns: -1,
    last_upstream_tx_byte_sent_ns: -1,
    first_upstream_rx_byte_received_ns: -1,
    first_upstream_rx_body_byte_received_ns: -1,
    last_upstream_rx_byte_received_ns: -1,
    first_downstream_tx_byte_sent_ns: -1,
    last_downstream_tx_byte_sent_ns: -1,
    last_downstream_ack_received_ns: -1,
    request_complete_duration_ns: -1,
    downstream_connection_end_ns: -1,
    has_start_time: false,
    has_downstream_connection_begin: false,
    has_downstream_handshake_start: false,
    has_downstream_handshake_complete: false,
    has_last_downstream_header_rx_byte_received: false,
    has_last_downstream_rx_byte_received: false,
    has_upstream_connect_start: false,
    has_upstream_connect_complete: false,
    has_upstream_handshake_complete: false,
    has_first_upstream_tx_byte_sent: false,
    has_last_upstream_tx_byte_sent: false,
    has_first_upstream_rx_byte_received: false,
    has_first_upstream_rx_body_byte_received: false,
    has_last_upstream_rx_byte_received: false,
    has_first_downstream_tx_byte_sent: false,
    has_last_downstream_tx_byte_sent: false,
    has_last_downstream_ack_received: false,
    has_request_complete: false,
    has_downstream_connection_end: false,
  }
}

#[cfg(test)]
mod tests {
  use super::*;

  #[test]
  fn unavailable_markers_remain_distinct_from_zero() {
    let info = abi::envoy_dynamic_module_type_timing_info_v2 {
      last_downstream_header_rx_byte_received_ns: 0,
      has_last_downstream_header_rx_byte_received: true,
      ..unavailable_timing_info()
    };

    let timing = TimingInfo::from(info);
    assert_eq!(timing.last_downstream_header_rx_byte_received_ns, Some(0));
    assert_eq!(timing.last_downstream_rx_byte_received_ns, None);
    assert_eq!(timing.downstream_connection_begin_ns, None);
    assert_eq!(
      TimingInfo::default().last_downstream_rx_byte_received_ns,
      None
    );
  }

  #[test]
  fn pre_request_offset_at_minus_one_nanosecond_is_available() {
    let info = abi::envoy_dynamic_module_type_timing_info_v2 {
      downstream_connection_begin_ns: -1,
      has_downstream_connection_begin: true,
      ..unavailable_timing_info()
    };

    let timing = TimingInfo::from(info);
    assert_eq!(timing.downstream_connection_begin_ns, Some(-1));
    assert_eq!(timing.downstream_handshake_start_ns, None);
  }
}
