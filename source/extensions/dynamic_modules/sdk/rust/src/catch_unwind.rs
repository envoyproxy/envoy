/// Opt-in panic guard that wraps a filter and catches panics at the trait boundary.
///
/// When a wrapped filter method panics, `CatchUnwind`:
/// 1. Drops the inner filter (preventing access to potentially inconsistent state).
/// 2. Logs the panic payload.
/// 3. Fails closed — sends a 500 response and/or returns `StopIteration` for HTTP filters, closes
///    the connection for network filters, etc.
///
/// Subsequent callbacks on a poisoned wrapper behave differently depending on type:
/// - Status-returning callbacks panic immediately. This indicates the fail-closed mechanism did not
///   terminate the stream as expected.
/// - Late async, event, and cleanup callbacks are silently skipped.
///
/// # Usage
///
/// ```
/// use envoy_proxy_dynamic_modules_rust_sdk::*;
///
/// struct MyFilter;
///
/// impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for MyFilter {}
///
/// struct MyFilterConfig;
///
/// impl<EHF: EnvoyHttpFilter> HttpFilterConfig<EHF> for MyFilterConfig {
///   fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
///     Box::new(CatchUnwind::new(MyFilter))
///   }
/// }
/// ```
pub struct CatchUnwind<F> {
  filter: Option<F>,
}

enum CatchError {
  Panicked,
  Poisoned,
}

impl<F> CatchUnwind<F> {
  pub fn new(filter: F) -> Self {
    Self {
      filter: Some(filter),
    }
  }

  /// Run `f` on the inner filter, catching any panic.
  ///
  /// If the filter was already poisoned by a prior panic, panics immediately — a
  /// status-returning callback on a poisoned filter means the fail-closed mechanism
  /// didn't terminate the stream as expected.
  fn catch<R>(&mut self, name: &str, f: impl FnOnce(&mut F) -> R) -> Result<R, ()> {
    let mut filter = self
      .filter
      .take()
      .expect("status-returning callback invoked on a poisoned filter");
    // AssertUnwindSafe is sound here: if a panic occurs, `filter` is not put back into
    // `self.filter` — it is dropped at the end of this scope, so no code ever observes
    // the potentially inconsistent state.
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| f(&mut filter))) {
      Ok(val) => {
        self.filter = Some(filter);
        Ok(val)
      },
      Err(panic) => {
        crate::envoy_log_error!(
          "{}: caught panic: {}",
          name,
          crate::panic_payload_to_string(panic)
        );
        Err(())
      },
    }
  }

  /// Like [`catch`](Self::catch), but skips if the filter is already poisoned.
  ///
  /// This is intended for async, event, and cleanup callbacks that Envoy may still
  /// invoke after a prior fail-closed.
  ///
  /// Returns:
  /// - `Ok(R)` if the callback completed successfully.
  /// - `Err(CatchError::Panicked)` if this invocation panicked and the caller should apply its
  ///   fail-closed action.
  /// - `Err(CatchError::Poisoned)` if the wrapper was already poisoned and the callback was
  ///   skipped.
  fn catch_or_skip<R>(&mut self, name: &str, f: impl FnOnce(&mut F) -> R) -> Result<R, CatchError> {
    let Some(mut filter) = self.filter.take() else {
      return Err(CatchError::Poisoned);
    };
    // See `catch` for AssertUnwindSafe justification.
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| f(&mut filter))) {
      Ok(val) => {
        self.filter = Some(filter);
        Ok(val)
      },
      Err(panic) => {
        crate::envoy_log_error!(
          "{}: caught panic: {}",
          name,
          crate::panic_payload_to_string(panic)
        );
        Err(CatchError::Panicked)
      },
    }
  }
}

use crate::abi;
use crate::buffer::EnvoyBuffer;
// ---------------------------------------------------------------------------
// HttpFilter
// ---------------------------------------------------------------------------
use crate::http::{EnvoyHttpFilter, HttpFilter};

/// Fail-closed 500 response sent when an HTTP filter panics on the request path.
fn send_500<EHF: EnvoyHttpFilter>(envoy: &mut EHF) {
  envoy.send_response(
    500,
    &[("content-type", b"text/plain")],
    Some(b"Internal Server Error: filter panic"),
    None,
  );
}

/// Reset the stream when an HTTP filter panics on the response path.
/// We can't send a 500 at this point because response headers may already be sent downstream.
fn reset_stream<EHF: EnvoyHttpFilter>(envoy: &mut EHF) {
  envoy.reset_stream(
    abi::envoy_dynamic_module_type_http_filter_stream_reset_reason::LocalReset,
    "filter panic",
  );
}

impl<EHF: EnvoyHttpFilter, F: HttpFilter<EHF>> HttpFilter<EHF> for CatchUnwind<F> {
  // Request-path panics: send a 500 local reply which terminates the downstream request.
  // sendLocalReply sets `sent_local_reply_` on the C++ side, preventing further filter
  // iteration on the request path. The `on_local_reply` callback is handled below.
  fn on_request_headers(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    self
      .catch("on_request_headers", |f| {
        f.on_request_headers(envoy_filter, end_of_stream)
      })
      .unwrap_or_else(|_| {
        send_500(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
      })
  }

  fn on_request_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    self
      .catch("on_request_body", |f| {
        f.on_request_body(envoy_filter, end_of_stream)
      })
      .unwrap_or_else(|_| {
        send_500(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationNoBuffer
      })
  }

  fn on_request_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    self
      .catch("on_request_trailers", |f| {
        f.on_request_trailers(envoy_filter)
      })
      .unwrap_or_else(|_| {
        send_500(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::StopIteration
      })
  }

  // Response-path panics: can't send a 500 because response headers may already be sent
  // downstream. Instead, reset the stream (LocalReset) which tears down the connection.
  fn on_response_headers(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    self
      .catch("on_response_headers", |f| {
        f.on_response_headers(envoy_filter, end_of_stream)
      })
      .unwrap_or_else(|_| {
        reset_stream(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration
      })
  }

  fn on_response_body(
    &mut self,
    envoy_filter: &mut EHF,
    end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    self
      .catch("on_response_body", |f| {
        f.on_response_body(envoy_filter, end_of_stream)
      })
      .unwrap_or_else(|_| {
        reset_stream(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_response_body_status::StopIterationNoBuffer
      })
  }

  fn on_response_trailers(
    &mut self,
    envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    self
      .catch("on_response_trailers", |f| {
        f.on_response_trailers(envoy_filter)
      })
      .unwrap_or_else(|_| {
        reset_stream(envoy_filter);
        abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::StopIteration
      })
  }

  // Void callbacks: cleanup/event notifications that Envoy may invoke after the stream is
  // already terminated. Safe to skip on a poisoned filter.
  fn on_stream_complete(&mut self, envoy_filter: &mut EHF) {
    let _ = self.catch_or_skip("on_stream_complete", |f| f.on_stream_complete(envoy_filter));
  }

  fn on_http_callout_done(
    &mut self,
    envoy_filter: &mut EHF,
    callout_id: u64,
    result: abi::envoy_dynamic_module_type_http_callout_result,
    response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    response_body: Option<&[EnvoyBuffer]>,
  ) {
    if matches!(
      self.catch_or_skip("on_http_callout_done", |f| {
        f.on_http_callout_done(
          envoy_filter,
          callout_id,
          result,
          response_headers,
          response_body,
        )
      }),
      Err(CatchError::Panicked)
    ) {
      reset_stream(envoy_filter);
    }
  }

  fn on_http_stream_headers(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: u64,
    response_headers: &[(EnvoyBuffer, EnvoyBuffer)],
    end_stream: bool,
  ) {
    if matches!(
      self.catch_or_skip("on_http_stream_headers", |f| {
        f.on_http_stream_headers(envoy_filter, stream_handle, response_headers, end_stream)
      }),
      Err(CatchError::Panicked)
    ) {
      reset_stream(envoy_filter);
    }
  }

  fn on_http_stream_data(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: u64,
    response_data: &[EnvoyBuffer],
    end_stream: bool,
  ) {
    if matches!(
      self.catch_or_skip("on_http_stream_data", |f| {
        f.on_http_stream_data(envoy_filter, stream_handle, response_data, end_stream)
      }),
      Err(CatchError::Panicked)
    ) {
      reset_stream(envoy_filter);
    }
  }

  fn on_http_stream_trailers(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: u64,
    response_trailers: &[(EnvoyBuffer, EnvoyBuffer)],
  ) {
    if matches!(
      self.catch_or_skip("on_http_stream_trailers", |f| {
        f.on_http_stream_trailers(envoy_filter, stream_handle, response_trailers)
      }),
      Err(CatchError::Panicked)
    ) {
      reset_stream(envoy_filter);
    }
  }

  fn on_http_stream_complete(&mut self, envoy_filter: &mut EHF, stream_handle: u64) {
    let _ = self.catch_or_skip("on_http_stream_complete", |f| {
      f.on_http_stream_complete(envoy_filter, stream_handle)
    });
  }

  fn on_http_stream_reset(
    &mut self,
    envoy_filter: &mut EHF,
    stream_handle: u64,
    reset_reason: abi::envoy_dynamic_module_type_http_stream_reset_reason,
  ) {
    let _ = self.catch_or_skip("on_http_stream_reset", |f| {
      f.on_http_stream_reset(envoy_filter, stream_handle, reset_reason)
    });
  }

  fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
    if matches!(
      self.catch_or_skip("on_scheduled", |f| f.on_scheduled(envoy_filter, event_id)),
      Err(CatchError::Panicked)
    ) {
      reset_stream(envoy_filter);
    }
  }

  fn on_downstream_above_write_buffer_high_watermark(&mut self, envoy_filter: &mut EHF) {
    let _ = self.catch_or_skip("on_downstream_above_write_buffer_high_watermark", |f| {
      f.on_downstream_above_write_buffer_high_watermark(envoy_filter)
    });
  }

  fn on_downstream_below_write_buffer_low_watermark(&mut self, envoy_filter: &mut EHF) {
    let _ = self.catch_or_skip("on_downstream_below_write_buffer_low_watermark", |f| {
      f.on_downstream_below_write_buffer_low_watermark(envoy_filter)
    });
  }

  // on_local_reply is invoked synchronously by sendLocalReply (triggered by send_500 above),
  // so it may be called while the filter is already poisoned. Must not abort in that case.
  fn on_local_reply(
    &mut self,
    envoy_filter: &mut EHF,
    response_code: u32,
    details: EnvoyBuffer,
    reset_imminent: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_local_reply_status {
    // send_500 triggers sendLocalReply synchronously, so this may be invoked while the
    // filter is already poisoned.
    self
      .catch_or_skip("on_local_reply", |f| {
        f.on_local_reply(envoy_filter, response_code, details, reset_imminent)
      })
      .unwrap_or(abi::envoy_dynamic_module_type_on_http_filter_local_reply_status::Continue)
  }
}

// ---------------------------------------------------------------------------
// NetworkFilter
// ---------------------------------------------------------------------------

use crate::network::{EnvoyNetworkFilter, NetworkFilter};

impl<ENF: EnvoyNetworkFilter, F: NetworkFilter<ENF>> NetworkFilter<ENF> for CatchUnwind<F> {
  // Data-path panics: close the connection (FlushWrite) to terminate gracefully.
  // After close, Envoy fires on_event(LocalClose) and on_destroy as cleanup.
  fn on_new_connection(
    &mut self,
    envoy_filter: &mut ENF,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self
      .catch("on_new_connection", |f| f.on_new_connection(envoy_filter))
      .unwrap_or_else(|_| {
        envoy_filter
          .close(abi::envoy_dynamic_module_type_network_connection_close_type::FlushWrite);
        abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
      })
  }

  fn on_read(
    &mut self,
    envoy_filter: &mut ENF,
    data_length: usize,
    end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self
      .catch("on_read", |f| {
        f.on_read(envoy_filter, data_length, end_stream)
      })
      .unwrap_or_else(|_| {
        envoy_filter
          .close(abi::envoy_dynamic_module_type_network_connection_close_type::FlushWrite);
        abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
      })
  }

  fn on_write(
    &mut self,
    envoy_filter: &mut ENF,
    data_length: usize,
    end_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_network_filter_data_status {
    self
      .catch("on_write", |f| {
        f.on_write(envoy_filter, data_length, end_stream)
      })
      .unwrap_or_else(|_| {
        envoy_filter
          .close(abi::envoy_dynamic_module_type_network_connection_close_type::FlushWrite);
        abi::envoy_dynamic_module_type_on_network_filter_data_status::StopIteration
      })
  }

  // Void callbacks: event/cleanup notifications that Envoy may invoke after close.
  fn on_event(
    &mut self,
    envoy_filter: &mut ENF,
    event: abi::envoy_dynamic_module_type_network_connection_event,
  ) {
    let _ = self.catch_or_skip("on_event", |f| f.on_event(envoy_filter, event));
  }

  fn on_http_callout_done(
    &mut self,
    envoy_filter: &mut ENF,
    callout_id: u64,
    result: abi::envoy_dynamic_module_type_http_callout_result,
    headers: Vec<(EnvoyBuffer, EnvoyBuffer)>,
    body_chunks: Vec<EnvoyBuffer>,
  ) {
    if matches!(
      self.catch_or_skip("on_http_callout_done", |f| {
        f.on_http_callout_done(envoy_filter, callout_id, result, headers, body_chunks)
      }),
      Err(CatchError::Panicked)
    ) {
      envoy_filter.close(abi::envoy_dynamic_module_type_network_connection_close_type::FlushWrite);
    }
  }

  fn on_destroy(&mut self, envoy_filter: &mut ENF) {
    let _ = self.catch_or_skip("on_destroy", |f| f.on_destroy(envoy_filter));
  }

  fn on_scheduled(&mut self, envoy_filter: &mut ENF, event_id: u64) {
    if matches!(
      self.catch_or_skip("on_scheduled", |f| f.on_scheduled(envoy_filter, event_id)),
      Err(CatchError::Panicked)
    ) {
      envoy_filter.close(abi::envoy_dynamic_module_type_network_connection_close_type::FlushWrite);
    }
  }

  fn on_above_write_buffer_high_watermark(&mut self, envoy_filter: &mut ENF) {
    let _ = self.catch_or_skip("on_above_write_buffer_high_watermark", |f| {
      f.on_above_write_buffer_high_watermark(envoy_filter)
    });
  }

  fn on_below_write_buffer_low_watermark(&mut self, envoy_filter: &mut ENF) {
    let _ = self.catch_or_skip("on_below_write_buffer_low_watermark", |f| {
      f.on_below_write_buffer_low_watermark(envoy_filter)
    });
  }
}

// ---------------------------------------------------------------------------
// ListenerFilter
// ---------------------------------------------------------------------------

use crate::listener::{EnvoyListenerFilter, ListenerFilter};

impl<ELF: EnvoyListenerFilter, F: ListenerFilter<ELF>> ListenerFilter<ELF> for CatchUnwind<F> {
  // Status-returning panics: close the socket to reject the connection immediately.
  fn on_accept(
    &mut self,
    envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    self
      .catch("on_accept", |f| f.on_accept(envoy_filter))
      .unwrap_or_else(|_| {
        envoy_filter.close_socket(Some("filter panic"));
        abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
      })
  }

  fn on_data(
    &mut self,
    envoy_filter: &mut ELF,
  ) -> abi::envoy_dynamic_module_type_on_listener_filter_status {
    self
      .catch("on_data", |f| f.on_data(envoy_filter))
      .unwrap_or_else(|_| {
        envoy_filter.close_socket(Some("filter panic"));
        abi::envoy_dynamic_module_type_on_listener_filter_status::StopIteration
      })
  }

  // Void callbacks: cleanup after the socket is already closed.
  fn on_close(&mut self, envoy_filter: &mut ELF) {
    let _ = self.catch_or_skip("on_close", |f| f.on_close(envoy_filter));
  }

  fn on_http_callout_done(
    &mut self,
    envoy_filter: &mut ELF,
    callout_id: u64,
    result: abi::envoy_dynamic_module_type_http_callout_result,
    response_headers: Vec<(EnvoyBuffer, EnvoyBuffer)>,
    response_body: Vec<EnvoyBuffer>,
  ) {
    if matches!(
      self.catch_or_skip("on_http_callout_done", |f| {
        f.on_http_callout_done(
          envoy_filter,
          callout_id,
          result,
          response_headers,
          response_body,
        )
      }),
      Err(CatchError::Panicked)
    ) {
      envoy_filter.close_socket(Some("filter panic"));
    }
  }

  fn on_scheduled(&mut self, envoy_filter: &mut ELF, event_id: u64) {
    if matches!(
      self.catch_or_skip("on_scheduled", |f| f.on_scheduled(envoy_filter, event_id)),
      Err(CatchError::Panicked)
    ) {
      envoy_filter.close_socket(Some("filter panic"));
    }
  }
}
