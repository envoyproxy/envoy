use crate::abi::envoy_dynamic_module_type_metrics_result;
use crate::buffer::{EnvoyBuffer, EnvoyMutBuffer};
use crate::utility::HeaderPairSlice;
use crate::{
  abi, bytes_to_module_buffer, str_to_module_buffer, ClusterHostCount, EnvoyCounterId,
  EnvoyCounterVecId, EnvoyGaugeId, EnvoyGaugeVecId, EnvoyHistogramId, EnvoyHistogramVecId,
  NewHttpFilterConfigFunction, NewHttpFilterPerRouteConfigFunction,
  NEW_HTTP_FILTER_CONFIG_FUNCTION, NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION,
};
use mockall::*;
use std::any::Any;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// The trait that represents the configuration for an Envoy Http filter configuration.
/// This has one to one mapping with the [`EnvoyHttpFilterConfig`] object.
///
/// The object is created when the corresponding Envoy Http filter config is created, and it is
/// dropped when the corresponding Envoy Http filter config is destroyed. Therefore, the
/// implementation is recommended to implement the [`Drop`] trait to handle the necessary cleanup.
///
/// Implementations must also be `Sync` since they are accessed from worker threads.
pub trait HttpFilterConfig<EHF: EnvoyHttpFilter>: Sync {
  /// This is called from a worker thread when a HTTP filter chain is created for a new stream.
  fn new_http_filter(&self, _envoy: &mut EHF) -> Box<dyn HttpFilter<EHF>> {
    panic!("not implemented");
  }

  /// This is called when the new event is scheduled via the
  /// [`EnvoyHttpFilterConfigScheduler::commit`] for this [`HttpFilterConfig`].
  ///
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyHttpFilterConfigScheduler::commit`] to distinguish multiple scheduled events.
  fn on_scheduled(&self, _event_id: u64) {}

  /// This is called when an HTTP callout initiated via
  /// [`EnvoyHttpFilterConfig::send_http_callout`] completes.
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `callout_id` is the opaque handle returned from
  ///   [`EnvoyHttpFilterConfig::send_http_callout`].
  /// * `result` indicates the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers.
  /// * `response_body` is the response body chunks.
  fn on_http_callout_done(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
  }

  /// This is called when response headers are received from an HTTP stream started via
  /// [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_headers` is a list of key-value pairs of the response headers.
  /// * `end_stream` indicates whether this is the final frame of the response.
  fn on_http_stream_headers(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _stream_handle: u64,
    _response_headers: &[(EnvoyBuffer, EnvoyBuffer)],
    _end_stream: bool,
  ) {
  }

  /// This is called when response data is received from an HTTP stream started via
  /// [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_data` is the response body data chunks.
  /// * `end_stream` indicates whether this is the final frame of the response.
  fn on_http_stream_data(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _stream_handle: u64,
    _response_data: &[EnvoyBuffer],
    _end_stream: bool,
  ) {
  }

  /// This is called when response trailers are received from an HTTP stream started via
  /// [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_trailers` is a list of key-value pairs of the response trailers.
  fn on_http_stream_trailers(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _stream_handle: u64,
    _response_trailers: &[(EnvoyBuffer, EnvoyBuffer)],
  ) {
  }

  /// This is called when an HTTP stream started via [`EnvoyHttpFilterConfig::start_http_stream`]
  /// completes successfully.
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `stream_handle` is the opaque handle to the HTTP stream (no longer valid after this call).
  fn on_http_stream_complete(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _stream_handle: u64,
  ) {
  }

  /// This is called when an HTTP stream started via [`EnvoyHttpFilterConfig::start_http_stream`]
  /// is reset (failed or cancelled).
  ///
  /// * `envoy_config` can be used to interact with the underlying Envoy filter config object.
  /// * `stream_handle` is the opaque handle to the HTTP stream (no longer valid after this call).
  /// * `reset_reason` indicates why the stream was reset.
  fn on_http_stream_reset(
    &self,
    _envoy_config: &mut EnvoyHttpFilterConfigImpl,
    _stream_handle: u64,
    _reset_reason: abi::envoy_dynamic_module_type_http_stream_reset_reason,
  ) {
  }
}

/// The trait that corresponds to an Envoy Http filter for each stream
/// created via the [`HttpFilterConfig::new_http_filter`] method.
///
/// All the event hooks are called on the same thread as the one that the [`HttpFilter`] is created
/// via the [`HttpFilterConfig::new_http_filter`] method. In other words, the [`HttpFilter`] object
/// is thread-local.
pub trait HttpFilter<EHF: EnvoyHttpFilter> {
  /// This is called when the request headers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_headers_status`] to
  /// indicate the status of the request headers processing.
  fn on_request_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::Continue
  }

  /// This is called when the request body is received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_body_status`] to
  /// indicate the status of the request body processing.
  fn on_request_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::Continue
  }

  /// This is called when the request trailers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status`] to
  /// indicate the status of the request trailers processing.
  fn on_request_trailers(
    &mut self,
    _envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::Continue
  }

  /// This is called when the response headers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_headers_status`] to
  /// indicate the status of the response headers processing.
  fn on_response_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::Continue
  }

  /// This is called when the response body is received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_body_status`] to
  /// indicate the status of the response body processing.
  fn on_response_body(
    &mut self,
    _envoy_filter: &mut EHF,
    _end_of_stream: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::Continue
  }

  /// This is called when the response trailers are received.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// The `end_of_stream` indicates whether the request is the last message in the stream.
  ///
  /// This must return [`abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status`] to
  /// indicate the status of the response trailers processing.
  fn on_response_trailers(
    &mut self,
    _envoy_filter: &mut EHF,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::Continue
  }

  /// This is called when the stream is complete.
  /// The `envoy_filter` can be used to interact with the underlying Envoy filter object.
  ///
  /// This is called before this [`HttpFilter`] object is dropped and access logs are flushed.
  fn on_stream_complete(&mut self, _envoy_filter: &mut EHF) {}

  /// This is called when the HTTP callout is done.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `callout_id` is the ID of the callout that was done.
  /// * `result` indicates the result of the callout.
  /// * `response_headers` is a list of key-value pairs of the response headers. This is optional.
  /// * `response_body` is the response body. This is optional.
  fn on_http_callout_done(
    &mut self,
    _envoy_filter: &mut EHF,
    _callout_id: u64,
    _result: abi::envoy_dynamic_module_type_http_callout_result,
    _response_headers: Option<&[(EnvoyBuffer, EnvoyBuffer)]>,
    _response_body: Option<&[EnvoyBuffer]>,
  ) {
  }

  /// This is called when response headers are received from an HTTP stream callout.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_headers` is a list of key-value pairs of the response headers.
  /// * `end_stream` indicates whether this is the final frame of the response.
  fn on_http_stream_headers(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: u64,
    _response_headers: &[(EnvoyBuffer, EnvoyBuffer)],
    _end_stream: bool,
  ) {
  }

  /// This is called when response data is received from an HTTP stream callout.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_data` is the response body data chunks.
  /// * `end_stream` indicates whether this is the final frame of the response.
  fn on_http_stream_data(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: u64,
    _response_data: &[EnvoyBuffer],
    _end_stream: bool,
  ) {
  }

  /// This is called when response trailers are received from an HTTP stream callout.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `stream_handle` is the opaque handle to the HTTP stream.
  /// * `response_trailers` is a list of key-value pairs of the response trailers.
  fn on_http_stream_trailers(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: u64,
    _response_trailers: &[(EnvoyBuffer, EnvoyBuffer)],
  ) {
  }

  /// This is called when an HTTP stream callout completes successfully.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `stream_handle` is the opaque handle to the HTTP stream (no longer valid after this call).
  fn on_http_stream_complete(&mut self, _envoy_filter: &mut EHF, _stream_handle: u64) {}

  /// This is called when an HTTP stream callout is reset (failed or cancelled).
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `stream_handle` is the opaque handle to the HTTP stream (no longer valid after this call).
  /// * `reset_reason` indicates why the stream was reset.
  fn on_http_stream_reset(
    &mut self,
    _envoy_filter: &mut EHF,
    _stream_handle: u64,
    _reset_reason: abi::envoy_dynamic_module_type_http_stream_reset_reason,
  ) {
  }

  /// This is called when the new event is scheduled via the [`EnvoyHttpFilterScheduler::commit`]
  /// for this [`HttpFilter`].
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `event_id` is the ID of the event that was scheduled with
  ///   [`EnvoyHttpFilterScheduler::commit`] to distinguish multiple scheduled events.
  ///
  /// See [`EnvoyHttpFilter::new_scheduler`] for more details on how to use this.
  fn on_scheduled(&mut self, _envoy_filter: &mut EHF, _event_id: u64) {}

  /// This is called when the downstream buffer size goes above the high watermark for a
  /// terminal filter.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  fn on_downstream_above_write_buffer_high_watermark(&mut self, _envoy_filter: &mut EHF) {}

  /// This is called when the downstream buffer size goes below the low watermark for a
  /// terminal filter.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  fn on_downstream_below_write_buffer_low_watermark(&mut self, _envoy_filter: &mut EHF) {}

  /// This is called when a local reply (error response) is being sent to the downstream.
  ///
  /// * `envoy_filter` can be used to interact with the underlying Envoy filter object.
  /// * `response_code` is the HTTP status code of the local reply.
  /// * `body` is the body content of the local reply.
  /// * `details` is the response code details string.
  ///
  /// Returns the action to take after the local reply hook completes:
  /// - Continue: Send the local reply as normal.
  /// - ContinueAndResetStream: Reset the stream instead of sending the local reply.
  fn on_local_reply(
    &mut self,
    _envoy_filter: &mut EHF,
    _response_code: u32,
    _details: EnvoyBuffer,
    _reset_imminent: bool,
  ) -> abi::envoy_dynamic_module_type_on_http_filter_local_reply_status {
    abi::envoy_dynamic_module_type_on_http_filter_local_reply_status::Continue
  }
}

/// An opaque object that represents the underlying Envoy Http filter config. This has one to one
/// mapping with the Envoy Http filter config object as well as [`HttpFilterConfig`] object.
pub trait EnvoyHttpFilterConfig {
  /// Define a new counter scoped to this filter config with the given name.
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result>;

  // Define a new counter vec scoped to this filter config with the given name.
  fn define_counter_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge scoped to this filter config with the given name.
  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec scoped to this filter config with the given name.
  fn define_gauge_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram scoped to this filter config with the given name.
  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec scoped to this filter config with the given name.
  fn define_histogram_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result>;

  /// Create a new implementation of the [`EnvoyHttpFilterConfigScheduler`] trait.
  ///
  /// This can be used to schedule an event to the main thread where the filter config is running.
  fn new_scheduler(&self) -> Box<dyn EnvoyHttpFilterConfigScheduler>;

  /// Perform an HTTP callout from the config context. The result will be delivered to
  /// [`HttpFilterConfig::on_http_callout_done`].
  fn send_http_callout<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64);

  /// Start an HTTP stream from the config context. Events will be delivered to
  /// [`HttpFilterConfig::on_http_stream_headers`], etc.
  fn start_http_stream<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    end_stream: bool,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64);

  /// Send data on an HTTP stream started via [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from
  ///   [`EnvoyHttpFilterConfig::start_http_stream`].
  unsafe fn send_http_stream_data(
    &mut self,
    stream_handle: u64,
    data: &[u8],
    end_stream: bool,
  ) -> bool;

  /// Send trailers on an HTTP stream started via [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from
  ///   [`EnvoyHttpFilterConfig::start_http_stream`].
  unsafe fn send_http_stream_trailers<'a>(
    &mut self,
    stream_handle: u64,
    trailers: &'a [(&'a str, &'a [u8])],
  ) -> bool;

  /// Reset an HTTP stream started via [`EnvoyHttpFilterConfig::start_http_stream`].
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from
  ///   [`EnvoyHttpFilterConfig::start_http_stream`].
  unsafe fn reset_http_stream(&mut self, stream_handle: u64);
}

pub struct EnvoyHttpFilterConfigImpl {
  pub(crate) raw_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
}

impl EnvoyHttpFilterConfig for EnvoyHttpFilterConfigImpl {
  fn define_counter(
    &mut self,
    name: &str,
  ) -> Result<EnvoyCounterId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_counter(
        self.raw_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_counter_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyCounterVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_counter(
        self.raw_ptr,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &mut self,
    name: &str,
  ) -> Result<EnvoyGaugeId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_gauge(
        self.raw_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_gauge_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyGaugeVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_gauge(
        self.raw_ptr,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &mut self,
    name: &str,
  ) -> Result<EnvoyHistogramId, envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_histogram(
        self.raw_ptr,
        str_to_module_buffer(name),
        std::ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn define_histogram_vec(
    &mut self,
    name: &str,
    labels: &[&str],
  ) -> Result<EnvoyHistogramVecId, envoy_dynamic_module_type_metrics_result> {
    let labels_ptr = labels.as_ptr();
    let labels_size = labels.len();
    let mut id: usize = 0;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_define_histogram(
        self.raw_ptr,
        str_to_module_buffer(name),
        labels_ptr as *const _ as *mut _,
        labels_size,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }

  fn new_scheduler(&self) -> Box<dyn EnvoyHttpFilterConfigScheduler> {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_http_filter_config_scheduler_new(self.raw_ptr);
      Box::new(EnvoyHttpFilterConfigSchedulerImpl {
        raw_ptr: scheduler_ptr,
      })
    }
  }

  fn send_http_callout<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();
    let mut callout_id: u64 = 0;

    let result = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_http_callout(
        self.raw_ptr,
        &mut callout_id as *mut _ as *mut _,
        str_to_module_buffer(cluster_name),
        headers_ptr as *const _ as *mut _,
        headers_len,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        timeout_milliseconds,
      )
    };

    (result, callout_id)
  }

  fn start_http_stream<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    end_stream: bool,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();
    let mut stream_id: u64 = 0;

    let result = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_start_http_stream(
        self.raw_ptr,
        &mut stream_id as *mut _ as *mut _,
        str_to_module_buffer(cluster_name),
        headers_ptr as *const _ as *mut _,
        headers_len,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        end_stream,
        timeout_milliseconds,
      )
    };

    (result, stream_id)
  }

  unsafe fn send_http_stream_data(
    &mut self,
    stream_handle: u64,
    data: &[u8],
    end_stream: bool,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_stream_send_data(
        self.raw_ptr,
        stream_handle,
        bytes_to_module_buffer(data),
        end_stream,
      )
    }
  }

  unsafe fn send_http_stream_trailers<'a>(
    &mut self,
    stream_handle: u64,
    trailers: &'a [(&'a str, &'a [u8])],
  ) -> bool {
    let HeaderPairSlice(trailers_ptr, trailers_len) = trailers.into();
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_stream_send_trailers(
        self.raw_ptr,
        stream_handle,
        trailers_ptr as *const _ as *mut _,
        trailers_len,
      )
    }
  }

  unsafe fn reset_http_stream(&mut self, stream_handle: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_reset_http_stream(
        self.raw_ptr,
        stream_handle,
      );
    }
  }
}
/// An opaque object that represents the underlying Envoy Http filter. This has one to one
/// mapping with the Envoy Http filter object as well as [`HttpFilter`] object per HTTP stream.
///
/// The Envoy filter object is inherently not thread-safe, and it is always recommended to
/// access it from the same thread as the one that [`HttpFilter`] event hooks are called.
#[automock]
#[allow(clippy::needless_lifetimes)] // Explicit lifetime specifiers are needed for mockall.
pub trait EnvoyHttpFilter {
  /// Get the value of the request header with the given key.
  /// If the header is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_request_header_values`] variant.
  fn get_request_header_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the request header with the given key.
  ///
  /// If the header is not found, this returns an empty vector.
  fn get_request_header_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all request headers.
  ///
  /// Returns a list of key-value pairs of the request headers.
  /// If there are no headers or headers are not available, this returns an empty list.
  fn get_request_headers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the request header with the given key and value.
  ///
  /// This will overwrite the existing value if the header is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the header is set successfully.
  fn set_request_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Add a new request header with the given key and value.
  ///
  /// This will add a new header even if the header with the same key already exists.
  ///
  /// Returns true if the header is added successfully.
  fn add_request_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the request header with the given key.
  ///
  /// Returns true if the header is removed successfully.
  fn remove_request_header(&mut self, key: &str) -> bool;

  /// Get the value of the request trailer with the given key.
  /// If the trailer is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_request_trailer_values`] variant.
  fn get_request_trailer_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the request trailer with the given key.
  ///
  /// If the trailer is not found, this returns an empty vector.
  fn get_request_trailer_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all request trailers.
  ///
  /// Returns a list of key-value pairs of the request trailers.
  /// If there are no trailers or trailers are not available, this returns an empty list.
  fn get_request_trailers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the request trailer with the given key and value.
  ///
  /// This will overwrite the existing value if the trailer is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the trailer is set successfully.
  fn set_request_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Add a new request trailer with the given key and value.
  ///
  /// This will add a new trailer even if the trailer with the same key already exists.
  ///
  /// Returns true if the trailer is added successfully.
  fn add_request_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the request trailer with the given key.
  ///
  /// Returns true if the trailer is removed successfully.
  fn remove_request_trailer(&mut self, key: &str) -> bool;

  /// Get the value of the response header with the given key.
  /// If the header is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_response_header_values`] variant.
  fn get_response_header_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the response header with the given key.
  ///
  /// If the header is not found, this returns an empty vector.
  fn get_response_header_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;

  /// Get all response headers.
  ///
  /// Returns a list of key-value pairs of the response headers.
  /// If there are no headers or headers are not available, this returns an empty list.
  fn get_response_headers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the response header with the given key and value.
  ///
  /// This will overwrite the existing value if the header is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the header is set successfully.
  fn set_response_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Add a new response header with the given key and value.
  ///
  /// This will add a new header even if the header with the same key already exists.
  ///
  /// Returns true if the header is added successfully.
  fn add_response_header(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the response header with the given key.
  ///
  /// Returns true if the header is removed successfully.
  fn remove_response_header(&mut self, key: &str) -> bool;

  /// Get the value of the response trailer with the given key.
  /// If the trailer is not found, this returns `None`.
  ///
  /// To handle multiple values for the same key, use
  /// [`EnvoyHttpFilter::get_response_trailer_values`] variant.
  fn get_response_trailer_value<'a>(&'a self, key: &str) -> Option<EnvoyBuffer<'a>>;

  /// Get the values of the response trailer with the given key.
  ///
  /// If the trailer is not found, this returns an empty vector.
  fn get_response_trailer_values<'a>(&'a self, key: &str) -> Vec<EnvoyBuffer<'a>>;
  /// Get all response trailers.
  ///
  /// Returns a list of key-value pairs of the response trailers.
  /// If there are no trailers or trailers are not available, this returns an empty list.
  fn get_response_trailers<'a>(&'a self) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>;

  /// Set the response trailer with the given key and value.
  ///
  /// This will overwrite the existing value if the trailer is already present.
  /// In case of multiple values for the same key, this will remove all the existing values and
  /// set the new value.
  ///
  /// Returns true if the operation is successful.
  fn set_response_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Add a new response trailer with the given key and value.
  ///
  /// This will add a new trailer even if the trailer with the same key already exists.
  ///
  /// Returns true if the trailer is added successfully.
  fn add_response_trailer(&mut self, key: &str, value: &[u8]) -> bool;

  /// Remove the response trailer with the given key.
  ///
  /// Returns true if the trailer is removed successfully.
  fn remove_response_trailer(&mut self, key: &str) -> bool;

  /// Send a response to the downstream with the given status code, headers, and body.
  ///
  /// The headers are passed as a list of key-value pairs.
  fn send_response<'a>(
    &mut self,
    status_code: u32,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    details: Option<&'a str>,
  );

  /// Send response headers to the downstream, optionally indicating end of stream.
  /// Necessary pseudo headers such as :status should be present.
  ///
  /// The headers are passed as a list of key-value pairs.
  fn send_response_headers<'a>(&mut self, headers: &'a [(&'a str, &'a [u8])], end_stream: bool);

  /// Send response body data to the downstream, optionally indicating end of stream.
  fn send_response_data<'a>(&mut self, body: &'a [u8], end_stream: bool);

  /// Send response trailers to the downstream. This implicitly ends the stream.
  ///
  /// The trailers are passed as a list of key-value pairs.
  fn send_response_trailers<'a>(&mut self, trailers: &'a [(&'a str, &'a [u8])]);

  /// add a custom flag to indicate a noteworthy event of this stream. Mutliple flags could be added
  /// and will be concatenated with comma. It should not contain any empty or space characters (' ',
  /// '\t', '\f', '\v', '\n', '\r'). to the HTTP stream. The flag can later be used in logging or
  /// metrics. Ideally, it should be a very short string that represents a single event, like the
  /// the Envoy response flag.
  fn add_custom_flag(&mut self, flag: &str);

  /// Get the number-typed metadata value with the given key.
  /// Use the `source` parameter to specify which metadata to use.
  /// If the metadata is not found or is the wrong type, this returns `None`.
  fn get_metadata_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<f64>;

  /// Set the number-typed dynamic metadata value with the given key.
  /// If the namespace is not found, this will create a new namespace.
  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64);

  /// Get the string-typed metadata value with the given key.
  /// Use the `source` parameter to specify which metadata to use.
  /// If the metadata is not found or is the wrong type, this returns `None`.
  fn get_metadata_string<'a>(
    &'a self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<EnvoyBuffer<'a>>;

  /// Set the string-typed dynamic metadata value with the given key.
  /// If the namespace is not found, this will create a new namespace.
  ///
  /// Returns true if the operation is successful.
  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str);

  /// Get the bool-typed metadata value with the given key.
  /// Use the `source` parameter to specify which metadata to use.
  /// If the metadata is not found or is the wrong type, this returns `None`.
  fn get_metadata_bool(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<bool>;

  /// Set the bool-typed dynamic metadata value with the given key.
  /// If the namespace is not found, this will create a new namespace.
  fn set_dynamic_metadata_bool(&mut self, namespace: &str, key: &str, value: bool);

  /// Get all keys in the given metadata namespace.
  /// Use the `source` parameter to specify which metadata to use.
  /// Returns a vector of `EnvoyBuffer` representing the key names,
  /// or `None` if the namespace is not found.
  fn get_metadata_keys<'a>(
    &'a self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
  ) -> Option<Vec<EnvoyBuffer<'a>>>;

  /// Get all namespace names in the metadata.
  /// Use the `source` parameter to specify which metadata to use.
  /// Returns a vector of `EnvoyBuffer` representing the namespace names,
  /// or `None` if no namespaces exist.
  fn get_metadata_namespaces<'a>(
    &'a self,
    source: abi::envoy_dynamic_module_type_metadata_source,
  ) -> Option<Vec<EnvoyBuffer<'a>>>;

  /// Append a number value to the dynamic metadata list stored under the given namespace and key.
  /// If the key does not exist, a new list is created. If the key exists but is not a list,
  /// or if the metadata is not accessible, this returns false.
  fn add_dynamic_metadata_list_number(&mut self, namespace: &str, key: &str, value: f64) -> bool;

  /// Append a string value to the dynamic metadata list stored under the given namespace and key.
  /// If the key does not exist, a new list is created. If the key exists but is not a list,
  /// or if the metadata is not accessible, this returns false.
  fn add_dynamic_metadata_list_string(&mut self, namespace: &str, key: &str, value: &str) -> bool;

  /// Append a bool value to the dynamic metadata list stored under the given namespace and key.
  /// If the key does not exist, a new list is created. If the key exists but is not a list,
  /// or if the metadata is not accessible, this returns false.
  fn add_dynamic_metadata_list_bool(&mut self, namespace: &str, key: &str, value: bool) -> bool;

  /// Get the number of elements in the metadata list stored under the given namespace and key.
  /// Use the `source` parameter to specify which metadata to use.
  /// Returns `None` if the metadata is not accessible, the namespace or key does not exist,
  /// or the value is not a list.
  fn get_metadata_list_size(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<usize>;

  /// Get the number value at the given index in the metadata list stored under the given namespace
  /// and key. Use the `source` parameter to specify which metadata to use.
  /// Returns `None` if the metadata is not accessible, the namespace or key does not exist,
  /// the value is not a list, the index is out of range, or the element is not a number.
  fn get_metadata_list_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<f64>;

  /// Get the string value at the given index in the metadata list stored under the given namespace
  /// and key. Use the `source` parameter to specify which metadata to use.
  /// Returns `None` if the metadata is not accessible, the namespace or key does not exist,
  /// the value is not a list, the index is out of range, or the element is not a string.
  ///
  /// The returned buffer's lifetime is tied to the current event hook.
  fn get_metadata_list_string<'a>(
    &'a self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<EnvoyBuffer<'a>>;

  /// Get the bool value at the given index in the metadata list stored under the given namespace
  /// and key. Use the `source` parameter to specify which metadata to use.
  /// Returns `None` if the metadata is not accessible, the namespace or key does not exist,
  /// the value is not a list, the index is out of range, or the element is not a bool.
  fn get_metadata_list_bool(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<bool>;

  /// Get the bytes-typed filter state value with the given key.
  /// If the filter state is not found or is the wrong type, this returns `None`.
  fn get_filter_state_bytes<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>>;

  /// Set the bytes-typed filter state value with the given key.
  /// If the filter state is not found, this will create a new filter state.
  ///
  /// Returns true if the operation is successful.
  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool;

  /// Set a typed filter state value with the given key. The value is deserialized by a
  /// registered `StreamInfo::FilterState::ObjectFactory` on the Envoy side. This is useful for
  /// setting filter state objects that other Envoy filters expect to read as specific C++ types
  /// (e.g., `PerConnectionCluster` used by TCP Proxy).
  ///
  /// Returns true if the operation is successful. This can fail if no ObjectFactory is registered
  /// for the key, if deserialization fails, or if the key already exists and is read-only.
  fn set_filter_state_typed(&mut self, key: &[u8], value: &[u8]) -> bool;

  /// Get the serialized value of a typed filter state object with the given key. The object must
  /// support `serializeAsString()` on the Envoy side.
  ///
  /// Returns None if the key does not exist, the object does not support serialization, or the
  /// filter state is not accessible.
  fn get_filter_state_typed<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>>;

  /// Get the received request body (the request body pieces received in the latest event).
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// The body is represented as a list of [`EnvoyBuffer`].
  /// Memory contents pointed by each [`EnvoyBuffer`] is mutable and can be modified in place.
  /// However, the vector itself is a "copied view". For example, adding or removing
  /// [`EnvoyBuffer`] from the vector has no effect on the underlying Envoy buffer. To write beyond
  /// the end of the buffer, use [`EnvoyHttpFilter::append_received_request_body`]. To remove data
  /// from the buffer, use [`EnvoyHttpFilter::drain_received_request_body`].
  ///
  /// To write completely new data, use [`EnvoyHttpFilter::drain_received_request_body`] for the
  /// size of the buffer, and then use [`EnvoyHttpFilter::append_received_request_body`] to write
  /// the new data.
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  ///
  /// // This is the test setup.
  /// let mut envoy_filter = MockEnvoyHttpFilter::default();
  /// // Mutable static storage is used for the test to simulate the response body operation.
  /// static mut BUFFER: [u8; 10] = *b"helloworld";
  /// envoy_filter
  ///   .expect_get_received_request_body()
  ///   .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
  /// envoy_filter
  ///   .expect_drain_received_request_body()
  ///   .return_const(true);
  ///
  ///
  /// // Calculate the size of the new received request body in bytes.
  /// let buffers = envoy_filter.get_received_request_body().unwrap();
  /// let mut size = 0;
  /// for buffer in &buffers {
  ///   size += buffer.as_slice().len();
  /// }
  /// assert_eq!(size, 10);
  ///
  /// // drain the new received request body.
  /// assert!(envoy_filter.drain_received_request_body(10));
  ///
  /// // Now start writing new data from the beginning of the request body.
  /// ```
  ///
  /// This returns None if the request body is not available.
  fn get_received_request_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Similar to [`EnvoyHttpFilter::get_received_request_body`], but returns the buffered request
  /// body (the request body pieces buffered so far in the filter chain).
  fn get_buffered_request_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Get the size of the received request body in bytes.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns None if the request body is not available.
  fn get_received_request_body_size(&mut self) -> usize;

  /// Similar to [`EnvoyHttpFilter::get_received_request_body_size`], but returns the size of the
  /// buffered request body in bytes.
  fn get_buffered_request_body_size(&mut self) -> usize;

  /// Drain the given number of bytes from the front of the received request body.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns false if the request body is not available.
  ///
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_received_request_body(&mut self, number_of_bytes: usize) -> bool;

  /// Similar to [`EnvoyHttpFilter::drain_received_request_body`], but drains from the buffered
  /// request body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the request body is not available.
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_buffered_request_body(&mut self, number_of_bytes: usize) -> bool;

  /// Append the given data to the end of the received request body.
  /// This should only be used in the [`HttpFilter::on_request_body`] callback.
  ///
  /// Returns false if the request body is not available.
  ///
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_received_request_body(&mut self, data: &[u8]) -> bool;

  /// Similar to [`EnvoyHttpFilter::append_received_request_body`], but appends to the buffered
  /// request body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the request body is not available.
  /// Note that after changing the request body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_buffered_request_body(&mut self, data: &[u8]) -> bool;

  /// Get the received response body (the response body pieces received in the latest event).
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// The body is represented as a list of
  /// [`EnvoyBuffer`]. Memory contents pointed by each [`EnvoyBuffer`] is mutable and can be
  /// modified in place. However, the buffer itself is immutable. For example, adding or removing
  /// [`EnvoyBuffer`] from the vector has no effect on the underlying Envoy buffer. To write the
  /// contents by changing its length, use [`EnvoyHttpFilter::drain_received_response_body`] or
  /// [`EnvoyHttpFilter::append_received_response_body`].
  ///
  /// To write completely new data, use [`EnvoyHttpFilter::drain_received_response_body`] for the
  /// size of the buffer, and then use [`EnvoyHttpFilter::append_received_response_body`] to write
  /// the new data.
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  ///
  /// // This is the test setup.
  /// let mut envoy_filter = MockEnvoyHttpFilter::default();
  /// // Mutable static storage is used for the test to simulate the response body operation.
  /// static mut BUFFER: [u8; 10] = *b"helloworld";
  /// envoy_filter
  ///   .expect_get_received_response_body()
  ///   .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
  /// envoy_filter
  ///   .expect_drain_received_response_body()
  ///   .return_const(true);
  ///
  ///
  /// // Calculate the size of the received response body in bytes.
  /// let buffers = envoy_filter.get_received_response_body().unwrap();
  /// let mut size = 0;
  /// for buffer in &buffers {
  ///   size += buffer.as_slice().len();
  /// }
  /// assert_eq!(size, 10);
  ///
  /// // drain the received response body.
  /// assert!(envoy_filter.drain_received_response_body(10));
  ///
  /// // Now start writing new data from the beginning of the request body.
  /// ```
  ///
  /// Returns None if the response body is not available.
  fn get_received_response_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Similar to [`EnvoyHttpFilter::get_received_response_body`], but returns the buffered response
  /// body (the response body pieces buffered so far in the filter chain).
  fn get_buffered_response_body<'a>(&'a mut self) -> Option<Vec<EnvoyMutBuffer<'a>>>;

  /// Get the size of the received response body in bytes.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns zero if the response body is not available or empty.
  fn get_received_response_body_size(&mut self) -> usize;

  /// Similar to [`EnvoyHttpFilter::get_received_response_body_size`], but returns the size of the
  /// buffered response body in bytes.
  ///
  /// Returns zero if the response body is not available or empty.
  fn get_buffered_response_body_size(&mut self) -> usize;

  /// Drain the given number of bytes from the front of the received response body.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns false if the response body is not available.
  ///
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_received_response_body(&mut self, number_of_bytes: usize) -> bool;

  /// Similar to [`EnvoyHttpFilter::drain_received_response_body`], but drains from the buffered
  /// response body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the response body is not available.
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn drain_buffered_response_body(&mut self, number_of_bytes: usize) -> bool;

  /// Append the given data to the end of the received response body.
  /// This should only be used in the [`HttpFilter::on_response_body`] callback.
  ///
  /// Returns false if the response body is not available.
  ///
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_received_response_body(&mut self, data: &[u8]) -> bool;

  /// Similar to [`EnvoyHttpFilter::append_received_response_body`], but appends to the buffered
  /// response body.
  ///
  /// This method should only be used by the final data-processing filter in the chain.
  /// In other words, a filter may safely modify the buffered body only if no later filters
  /// in the chain have accessed it yet.
  ///
  /// Returns false if the response body is not available.
  /// Note that after changing the response body, it is caller's responsibility to modify the
  /// content-length header if necessary.
  fn append_buffered_response_body(&mut self, data: &[u8]) -> bool;

  /// Returns true if the latest received request body is the previously buffered request body.
  ///
  /// This is true when a previous filter in the chain stopped and buffered the request body,
  /// then resumed, and this filter is now receiving that buffered body.
  ///
  /// NOTE: This is only meaningful inside [`HttpFilter::on_request_body`].
  fn received_buffered_request_body(&mut self) -> bool;

  /// Returns true if the latest received response body is the previously buffered response body.
  ///
  /// This is true when a previous filter in the chain stopped and buffered the response body,
  /// then resumed, and this filter is now receiving that buffered body.
  ///
  /// NOTE: This is only meaningful inside [`HttpFilter::on_response_body`].
  fn received_buffered_response_body(&mut self) -> bool;

  /// Clear the route cache calculated during a previous phase of the filter chain.
  ///
  /// This is useful when the filter wants to force a re-evaluation of the route selection after
  /// modifying the request headers, etc that affect the routing decision.
  fn clear_route_cache(&mut self);

  /// Get the value of the attribute with the given ID as a string.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  fn get_attribute_string<'a>(
    &'a self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'a>>;

  /// Get the value of the attribute with the given ID as an integer.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<i64>;

  /// Get the value of the attribute with the given ID as a boolean.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  fn get_attribute_bool(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<bool>;

  /// Send an HTTP callout to the given cluster with the given headers and body.
  /// Multiple callouts can be made from the same filter. Different callouts can be
  /// distinguished by the returned callout id.
  ///
  /// Headers must contain the `:method`, ":path", and `host` headers.
  ///
  /// This returns the status and callout id of the callout. The id is used to
  /// distinguish different callouts made from the same filter and is generated by Envoy.
  /// The meaning of the status is:
  ///
  ///   * Success: The callout was sent successfully.
  ///   * MissingRequiredHeaders: One of the required headers is missing: `:method`, `:path`, or
  ///     `host`.
  ///   * ClusterNotFound: The cluster with the given name was not found.
  ///   * DuplicateCalloutId: The callout ID is already in use.
  ///   * CouldNotCreateRequest: The request could not be created. This happens when, for example,
  ///     there's no healthy upstream host in the cluster.
  ///
  /// The callout result will be delivered to the [`HttpFilter::on_http_callout_done`] method.
  fn send_http_callout<'a>(
    &mut self,
    _cluster_name: &'a str,
    _headers: &'a [(&'a str, &'a [u8])],
    _body: Option<&'a [u8]>,
    _timeout_milliseconds: u64,
  ) -> (
    abi::envoy_dynamic_module_type_http_callout_init_result,
    u64, // callout handle
  );

  /// Start a streamable HTTP callout to the given cluster with the given headers and optional
  /// body. Multiple concurrent streams can be created from the same filter.
  ///
  /// Headers must contain the `:method`, `:path`, and `host` headers.
  ///
  /// This returns a tuple of (status, stream_handle):
  ///   * Success + valid stream_handle: The stream was started successfully.
  ///   * MissingRequiredHeaders + null: One of the required headers is missing.
  ///   * ClusterNotFound + null: The cluster with the given name was not found.
  ///   * CannotCreateRequest + null: The stream could not be created (e.g., no healthy upstream).
  ///
  /// After starting the stream, use the returned stream_handle to send data/trailers or reset.
  /// Stream events will be delivered to the [`HttpFilter::on_http_stream_*`] methods.
  ///
  /// When the HTTP stream ends (either successfully or via reset), no further callbacks will
  /// be invoked for that stream.
  fn start_http_stream<'a>(
    &mut self,
    _cluster_name: &'a str,
    _headers: &'a [(&'a str, &'a [u8])],
    _body: Option<&'a [u8]>,
    _end_stream: bool,
    _timeout_milliseconds: u64,
  ) -> (
    abi::envoy_dynamic_module_type_http_callout_init_result,
    u64, // stream handle
  );

  /// Send data on an active HTTP stream.
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from [`EnvoyHttpFilter::start_http_stream`].
  /// * `data` is the data to send.
  /// * `end_stream` indicates whether this is the final frame of the request.
  ///
  /// Returns true if the data was sent successfully, false otherwise.
  unsafe fn send_http_stream_data(
    &mut self,
    _stream_handle: u64,
    _data: &[u8],
    _end_stream: bool,
  ) -> bool;

  /// Send trailers on an active HTTP stream (implicitly ends the stream).
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from [`EnvoyHttpFilter::start_http_stream`].
  /// * `trailers` is a list of key-value pairs for the trailers.
  ///
  /// Returns true if the trailers were sent successfully, false otherwise.
  unsafe fn send_http_stream_trailers<'a>(
    &mut self,
    _stream_handle: u64,
    _trailers: &'a [(&'a str, &'a [u8])],
  ) -> bool;

  /// Reset (cancel) an active HTTP stream.
  ///
  /// # Safety
  ///
  /// * `stream_handle` must be a valid handle returned from [`EnvoyHttpFilter::start_http_stream`].
  ///
  /// This will trigger the [`HttpFilter::on_http_stream_reset`] callback.
  unsafe fn reset_http_stream(&mut self, _stream_handle: u64);

  /// Get the most specific route configuration for the current route.
  ///
  /// Returns None if no per-route configuration is present on this route. Otherwise,
  /// returns the most specific per-route configuration (i.e. the one most up along the config
  /// hierarchy) created by the filter.
  fn get_most_specific_route_config(&self) -> Option<std::sync::Arc<dyn Any>>;

  /// This can be called to continue the decoding of the HTTP request when the processing is
  /// stopped.
  ///
  /// For example, this can be used inside the [`HttpFilter::on_http_callout_done`] or
  /// [`HttpFilter::on_scheduled`] methods to continue the decoding of the request body
  /// after the callout or scheduled event is done.
  fn continue_decoding(&mut self);

  /// This is exactly the same as [`EnvoyHttpFilter::continue_decoding`], but it is
  /// used to continue the encoding of the HTTP response.
  fn continue_encoding(&mut self);

  /// Create a new implementation of the [`EnvoyHttpFilterScheduler`] trait.
  ///
  /// ## Example Usage
  ///
  /// ```
  /// use abi::*;
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  /// use std::thread;
  ///
  /// struct TestFilter;
  /// impl<EHF: EnvoyHttpFilter> HttpFilter<EHF> for TestFilter {
  ///   fn on_request_headers(
  ///     &mut self,
  ///     envoy_filter: &mut EHF,
  ///     _end_of_stream: bool,
  ///   ) -> envoy_dynamic_module_type_on_http_filter_request_headers_status {
  ///     let scheduler = envoy_filter.new_scheduler();
  ///     let _ = std::thread::spawn(move || {
  ///       // Do some work in a separate thread.
  ///       // ...
  ///       // Then schedule the event to continue processing.
  ///       scheduler.commit(12345);
  ///     });
  ///     // Stops the iteration and schedules the event from the separate thread.
  ///     envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  ///   }
  ///   fn on_scheduled(&mut self, envoy_filter: &mut EHF, event_id: u64) {
  ///     // The event_id should match the one we scheduled.
  ///     assert_eq!(event_id, 12345);
  ///     // Then we can continue processing the request.
  ///     envoy_filter.continue_decoding();
  ///   }
  /// }
  /// ```
  fn new_scheduler(&self) -> impl EnvoyHttpFilterScheduler + 'static;

  /// Increment the counter with the given id.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increment the counter vec with the given id.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge with the given id.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Increase the gauge vec with the given id.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge with the given id.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Decrease the gauge vec with the given id.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge with the given id.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Set the value of the gauge vec with the given id.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram with the given id.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Record a value in the histogram vec with the given id.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&'a str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result>;

  /// Get the index of the current worker thread.
  fn get_worker_index(&self) -> u32;

  /// Set an integer socket option with the given level, name, state, and direction.
  /// Direction specifies whether to apply to upstream (outgoing to backend) or
  /// downstream (incoming from client) socket.
  fn set_socket_option_int(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
    value: i64,
  ) -> bool;

  /// Set a bytes socket option with the given level, name, state, and direction.
  /// Direction specifies whether to apply to upstream (outgoing to backend) or
  /// downstream (incoming from client) socket.
  fn set_socket_option_bytes(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
    value: &[u8],
  ) -> bool;

  /// Get an integer socket option value.
  fn get_socket_option_int(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
  ) -> Option<i64>;

  /// Get a bytes socket option value.
  fn get_socket_option_bytes(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
  ) -> Option<Vec<u8>>;

  // ------------------- Buffer limit methods -------------------------

  /// Get the current buffer limit for body data.
  ///
  /// This is the maximum amount of data that can be buffered for body data before backpressure
  /// is applied. A buffer limit of 0 bytes indicates no limits are applied.
  fn get_buffer_limit(&self) -> u64;

  /// Set the buffer limit for body data.
  ///
  /// This controls the maximum amount of data that can be buffered for body data before
  /// backpressure is applied.
  ///
  /// It is recommended (but not required) that filters calling this function should generally
  /// only perform increases to the buffer limit, to avoid potentially conflicting with the
  /// buffer requirements of other filters in the chain. For example:
  ///
  /// ```
  /// use envoy_proxy_dynamic_modules_rust_sdk::*;
  ///
  /// let mut envoy_filter = MockEnvoyHttpFilter::default();
  /// envoy_filter.expect_get_buffer_limit().return_const(0u64);
  /// envoy_filter.expect_set_buffer_limit().return_const(());
  /// let desired_limit: u64 = 1024;
  /// if desired_limit > envoy_filter.get_buffer_limit() {
  ///   envoy_filter.set_buffer_limit(desired_limit);
  /// }
  /// ```
  fn set_buffer_limit(&mut self, limit: u64);

  // ----------------------------- Tracing methods -----------------------------

  /// Get the active tracing span for the current HTTP stream.
  ///
  /// Returns `Some(Box<dyn EnvoySpan>)` if tracing is enabled and a span is available,
  /// otherwise returns `None`.
  ///
  /// The returned span can be used to add tags, logs, or spawn child spans.
  /// The active span is managed by Envoy and should not be finished by the module.
  fn get_active_span<'a>(&'a self) -> Option<Box<dyn EnvoySpan + 'a>>;

  /// Create a child span from the active span with the given operation name.
  ///
  /// Returns `Some(Box<dyn EnvoyChildSpan>)` if the child span was created successfully,
  /// otherwise returns `None`.
  ///
  /// The returned child span must be finished by calling [`EnvoyChildSpan::finish`]
  /// when done. Failing to finish the span will result in incomplete trace data.
  fn spawn_child_span<'a>(&'a self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + 'a>>;

  // ------------------- Cluster/Upstream Information methods -------------------------

  /// Get the name of the cluster that the current request is routed to.
  ///
  /// Returns `None` if no route has been selected yet or if the cluster information
  /// is not available.
  ///
  /// This is useful for making routing decisions or for logging.
  fn get_cluster_name<'a>(&'a self) -> Option<EnvoyBuffer<'a>>;

  /// Get host counts for the cluster that the current request is routed to.
  ///
  /// Returns a tuple of (total_count, healthy_count, degraded_count) for the specified
  /// priority level. Returns `None` if the cluster is not available or if the priority
  /// level does not exist.
  ///
  /// This is useful for implementing scale-to-zero logic or custom load balancing decisions.
  fn get_cluster_host_count(&self, priority: u32) -> Option<ClusterHostCount>;

  /// Set the override host to be used by the upstream load balancer.
  ///
  /// If the target host exists in the host list of the routed cluster, this host should
  /// be selected first. This is useful for implementing sticky sessions, host affinity,
  /// or custom load balancing logic.
  ///
  /// * `host` - The host address to override (e.g., "10.0.0.1:8080"). Must be a valid IP address.
  /// * `strict` - If true, the request will fail if the override host is not available. If false,
  ///   normal load balancing will be used as a fallback.
  ///
  /// Returns `true` if the override was set successfully, `false` if the host address is invalid.
  fn set_upstream_override_host(&mut self, host: &str, strict: bool) -> bool;

  // ------------------- Stream Control methods -------------------------

  /// Reset the HTTP stream with the specified reason.
  ///
  /// This is useful for terminating the stream when an error condition is detected or when
  /// the filter needs to abort processing.
  ///
  /// After calling this function, no further filter callbacks will be invoked for this stream
  /// except for the destroy callback.
  ///
  /// * `reason` - The reason for resetting the stream.
  /// * `details` - Details string explaining the reset reason. Can be empty.
  fn reset_stream(
    &mut self,
    reason: abi::envoy_dynamic_module_type_http_filter_stream_reset_reason,
    details: &str,
  );

  /// Attempt to send a GOAWAY frame to the downstream and close the connection.
  ///
  /// This is a connection-level operation that affects all streams on the connection. When called,
  /// the current filter chain iteration is aborted immediately and no subsequent filters will be
  /// invoked for the current callback. This effectively pauses all streams on the connection.
  ///
  /// # Protocol-specific behavior
  ///
  /// - **HTTP/2**: Sends an actual GOAWAY frame to notify the client that the connection is being
  ///   closed and no new streams should be initiated.
  /// - **HTTP/1**: Since HTTP/1 does not have a GOAWAY frame, the connection is closed directly.
  ///   The `graceful` parameter still controls whether in-flight requests are allowed to complete.
  ///
  /// # Arguments
  ///
  /// * `graceful` - If true, initiates a graceful drain sequence that allows in-flight streams to
  ///   complete before closing the connection. If false, sends GOAWAY (for HTTP/2) and closes the
  ///   connection immediately.
  ///
  /// # Filter chain behavior
  ///
  /// Once called, the filter chain iteration is stopped and no further filters will process
  /// the current request/response. The stream may still receive the `on_destroy` callback for
  /// cleanup purposes.
  ///
  /// # Multiple calls
  ///
  /// Multiple calls to this method are safe and the operation is idempotent. Subsequent calls are
  /// no-ops once the first call has been made.
  fn send_go_away_and_close(&mut self, graceful: bool);

  /// Recreate the HTTP stream, optionally with new headers.
  ///
  /// This is useful for implementing internal redirects or request retries.
  ///
  /// After calling this function successfully, the current filter chain will be destroyed and a new
  /// stream will be created. The filter should return StopIteration from the current event hook.
  ///
  /// * `headers` - Optional list of new headers for the recreated stream. If None, the original
  ///   headers will be reused.
  ///
  /// Returns `true` if the stream recreation was initiated successfully, `false` otherwise (e.g.,
  /// if the request body has not been fully received yet or if the stream cannot be recreated).
  fn recreate_stream<'a>(&mut self, headers: Option<&'a [(&'a str, &'a [u8])]>) -> bool;

  /// Clear only the cluster selection for the current route without clearing the entire route
  /// cache.
  ///
  /// This is a subset of [`EnvoyHttpFilter::clear_route_cache`]. Use this when a filter modifies
  /// headers that affect cluster selection but not the route itself. This is more efficient than
  /// clearing the entire route cache.
  fn clear_route_cluster_cache(&mut self);
}

/// Trait representing a tracing span.
///
/// This trait provides methods to interact with a tracing span, such as setting tags,
/// logging events, and spawning child spans.
///
/// The span is managed by Envoy and should not be finished by the module.
pub trait EnvoySpan {
  /// Set a tag on this span.
  ///
  /// Tags are key-value pairs that provide metadata about the span.
  fn set_tag(&self, key: &str, value: &str);

  /// Set the operation name on this span.
  fn set_operation(&self, operation: &str);

  /// Log an event on this span with the current timestamp.
  fn log(&self, event: &str);

  /// Override the sampling decision for this span.
  ///
  /// If sampled is false, this span and any subsequent child spans will not be
  /// reported to the tracing system.
  fn set_sampled(&self, sampled: bool);

  /// Get a baggage value from this span.
  ///
  /// Baggage data may have been set by this span or any parent spans.
  /// Returns `None` if the key was not found.
  ///
  /// Note: The returned string is temporary and should be copied if needed
  /// beyond immediate use.
  fn get_baggage(&self, key: &str) -> Option<String>;

  /// Set a baggage value on this span.
  ///
  /// All subsequent child spans will have access to this baggage.
  fn set_baggage(&self, key: &str, value: &str);

  /// Get the trace ID from this span.
  ///
  /// Returns `None` if the trace ID is not available.
  ///
  /// Note: The returned string is temporary and should be copied if needed
  /// beyond immediate use.
  fn get_trace_id(&self) -> Option<String>;

  /// Get the span ID from this span.
  ///
  /// Returns `None` if the span ID is not available.
  ///
  /// Note: The returned string is temporary and should be copied if needed
  /// beyond immediate use.
  fn get_span_id(&self) -> Option<String>;

  /// Create a child span with the given operation name.
  ///
  /// The child span must be finished by calling [`EnvoyChildSpan::finish`] when done.
  fn spawn_child(&self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + '_>>;
}

/// Implementation of [`EnvoySpan`] that wraps the raw span pointer from Envoy.
struct EnvoySpanImpl {
  raw_ptr: abi::envoy_dynamic_module_type_span_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
}

impl EnvoySpan for EnvoySpanImpl {
  fn set_tag(&self, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_tag(
        self.raw_ptr,
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      );
    }
  }

  fn set_operation(&self, operation: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_operation(
        self.raw_ptr,
        str_to_module_buffer(operation),
      );
    }
  }

  fn log(&self, event: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_log(
        self.filter_ptr,
        self.raw_ptr,
        str_to_module_buffer(event),
      );
    }
  }

  fn set_sampled(&self, sampled: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_sampled(self.raw_ptr, sampled);
    }
  }

  fn get_baggage(&self, key: &str) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_span_get_baggage(
        self.raw_ptr,
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(String::from_utf8_lossy(slice).to_string())
    } else {
      None
    }
  }

  fn set_baggage(&self, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_baggage(
        self.raw_ptr,
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      );
    }
  }

  fn get_trace_id(&self) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_span_get_trace_id(
        self.raw_ptr,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(String::from_utf8_lossy(slice).to_string())
    } else {
      None
    }
  }

  fn get_span_id(&self) -> Option<String> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_span_get_span_id(
        self.raw_ptr,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(String::from_utf8_lossy(slice).to_string())
    } else {
      None
    }
  }

  fn spawn_child(&self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + '_>> {
    let raw_ptr = unsafe {
      abi::envoy_dynamic_module_callback_http_span_spawn_child(
        self.filter_ptr,
        self.raw_ptr,
        str_to_module_buffer(operation_name),
      )
    };
    if raw_ptr.is_null() {
      None
    } else {
      Some(Box::new(EnvoyChildSpanImpl {
        raw_ptr,
        filter_ptr: self.filter_ptr,
        finished: false,
      }))
    }
  }
}

/// Trait representing a child tracing span created by the module.
///
/// Child spans are owned by the module and must be finished by calling
/// [`EnvoyChildSpan::finish`] when done.
pub trait EnvoyChildSpan {
  /// Set a tag on this span.
  fn set_tag(&self, key: &str, value: &str);

  /// Set the operation name on this span.
  fn set_operation(&self, operation: &str);

  /// Log an event on this span with the current timestamp.
  fn log(&self, event: &str);

  /// Override the sampling decision for this span.
  fn set_sampled(&self, sampled: bool);

  /// Set a baggage value on this span.
  fn set_baggage(&self, key: &str, value: &str);

  /// Create a child span from this span with the given operation name.
  fn spawn_child(&self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + '_>>;

  /// Finish and release this span.
  ///
  /// After calling this method, the span is no longer valid and should not be used.
  /// Note: This takes `&mut self` instead of `self` to maintain object-safety.
  /// The implementation should ensure the span is not used after this call.
  fn finish(&mut self);
}

/// Implementation of [`EnvoyChildSpan`] that wraps the raw span pointer.
struct EnvoyChildSpanImpl {
  raw_ptr: abi::envoy_dynamic_module_type_child_span_module_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  finished: bool,
}

impl EnvoyChildSpan for EnvoyChildSpanImpl {
  fn set_tag(&self, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_tag(
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      );
    }
  }

  fn set_operation(&self, operation: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_operation(
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        str_to_module_buffer(operation),
      );
    }
  }

  fn log(&self, event: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_log(
        self.filter_ptr,
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        str_to_module_buffer(event),
      );
    }
  }

  fn set_sampled(&self, sampled: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_sampled(
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        sampled,
      );
    }
  }

  fn set_baggage(&self, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_span_set_baggage(
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      );
    }
  }

  fn spawn_child(&self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + '_>> {
    let raw_ptr = unsafe {
      abi::envoy_dynamic_module_callback_http_span_spawn_child(
        self.filter_ptr,
        self.raw_ptr as abi::envoy_dynamic_module_type_span_envoy_ptr,
        str_to_module_buffer(operation_name),
      )
    };
    if raw_ptr.is_null() {
      None
    } else {
      Some(Box::new(EnvoyChildSpanImpl {
        raw_ptr,
        filter_ptr: self.filter_ptr,
        finished: false,
      }))
    }
  }

  fn finish(&mut self) {
    if !self.finished {
      unsafe {
        abi::envoy_dynamic_module_callback_http_child_span_finish(self.raw_ptr);
      }
      self.finished = true;
    }
  }
}

impl Drop for EnvoyChildSpanImpl {
  fn drop(&mut self) {
    // If the span was not explicitly finished, finish it now.
    if !self.finished {
      unsafe {
        abi::envoy_dynamic_module_callback_http_child_span_finish(self.raw_ptr);
      }
    }
  }
}

/// This implements the [`EnvoyHttpFilter`] trait with the given raw pointer to the Envoy HTTP
/// filter object.
///
/// This is not meant to be used directly.
pub struct EnvoyHttpFilterImpl {
  pub(crate) raw_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
}

impl EnvoyHttpFilter for EnvoyHttpFilterImpl {
  fn get_request_header_value(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
    )
  }

  fn get_request_header_values(&self, key: &str) -> Vec<EnvoyBuffer<'_>> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
    )
  }

  fn get_request_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    self.get_headers_impl(abi::envoy_dynamic_module_type_http_header_type::RequestHeader)
  }

  fn set_request_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn add_request_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn get_request_trailer_value(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::RequestTrailer,
    )
  }

  fn get_request_trailer_values(&self, key: &str) -> Vec<EnvoyBuffer<'_>> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::RequestTrailer,
    )
  }

  fn get_request_trailers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    self.get_headers_impl(abi::envoy_dynamic_module_type_http_header_type::RequestTrailer)
  }

  fn set_request_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestTrailer,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn add_request_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestTrailer,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn get_response_header_value(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
    )
  }

  fn get_response_header_values(&self, key: &str) -> Vec<EnvoyBuffer<'_>> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
    )
  }

  fn get_response_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    self.get_headers_impl(abi::envoy_dynamic_module_type_http_header_type::ResponseHeader)
  }

  fn set_response_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn add_response_header(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn get_response_trailer_value(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self.get_header_value_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
    )
  }

  fn get_response_trailer_values(&self, key: &str) -> Vec<EnvoyBuffer<'_>> {
    self.get_header_values_impl(
      key,
      abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
    )
  }

  fn get_response_trailers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    self.get_headers_impl(abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer)
  }

  fn set_response_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn add_response_trailer(&mut self, key: &str, value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
        str_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn send_response(
    &mut self,
    status_code: u32,
    headers: &[(&str, &[u8])],
    body: Option<&[u8]>,
    details: Option<&str>,
  ) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let details_ptr = details.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let details_length = details.map(|s| s.len()).unwrap_or(0);

    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response(
        self.raw_ptr,
        status_code,
        headers_ptr as *mut _,
        headers_len,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: details_ptr as *mut _,
          length: details_length,
        },
      )
    }
  }

  fn send_response_headers(&mut self, headers: &[(&str, &[u8])], end_stream: bool) {
    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_headers(
        self.raw_ptr,
        headers_ptr as *mut _,
        headers_len,
        end_stream,
      )
    }
  }

  fn send_response_data(&mut self, body: &[u8], end_stream: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_data(
        self.raw_ptr,
        bytes_to_module_buffer(body),
        end_stream,
      )
    }
  }

  fn send_response_trailers(&mut self, trailers: &[(&str, &[u8])]) {
    let HeaderPairSlice(trailers_ptr, trailers_len) = trailers.into();

    unsafe {
      abi::envoy_dynamic_module_callback_http_send_response_trailers(
        self.raw_ptr,
        trailers_ptr as *mut _,
        trailers_len,
      )
    }
  }

  fn add_custom_flag(&mut self, flag: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_custom_flag(
        self.raw_ptr,
        str_to_module_buffer(flag),
      );
    }
  }

  fn get_metadata_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<f64> {
    let mut value: f64 = 0f64;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_number(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut value as *mut _ as *mut _,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn set_dynamic_metadata_number(&mut self, namespace: &str, key: &str, value: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn get_metadata_string(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_string(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn set_dynamic_metadata_string(&mut self, namespace: &str, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      )
    }
  }

  fn get_metadata_bool(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<bool> {
    let mut value: bool = false;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_bool(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut value as *mut _ as *mut _,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn set_dynamic_metadata_bool(&mut self, namespace: &str, key: &str, value: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_dynamic_metadata_bool(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn get_metadata_keys(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
  ) -> Option<Vec<EnvoyBuffer<'_>>> {
    let count = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_keys_count(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
      )
    };
    if count == 0 {
      return None;
    }
    let mut buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      count
    ];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_keys(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        buffers.as_mut_ptr() as *mut _,
      )
    };
    if success {
      Some(
        buffers
          .into_iter()
          .map(|b| unsafe { EnvoyBuffer::new_from_raw(b.ptr as *const _, b.length) })
          .collect(),
      )
    } else {
      None
    }
  }

  fn get_metadata_namespaces(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
  ) -> Option<Vec<EnvoyBuffer<'_>>> {
    let count = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_namespaces_count(self.raw_ptr, source)
    };
    if count == 0 {
      return None;
    }
    let mut buffers: Vec<abi::envoy_dynamic_module_type_envoy_buffer> = vec![
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      count
    ];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_namespaces(
        self.raw_ptr,
        source,
        buffers.as_mut_ptr() as *mut _,
      )
    };
    if success {
      Some(
        buffers
          .into_iter()
          .map(|b| unsafe { EnvoyBuffer::new_from_raw(b.ptr as *const _, b.length) })
          .collect(),
      )
    } else {
      None
    }
  }

  fn add_dynamic_metadata_list_number(&mut self, namespace: &str, key: &str, value: f64) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn add_dynamic_metadata_list_string(&mut self, namespace: &str, key: &str, value: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        str_to_module_buffer(value),
      )
    }
  }

  fn add_dynamic_metadata_list_bool(&mut self, namespace: &str, key: &str, value: bool) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool(
        self.raw_ptr,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        value,
      )
    }
  }

  fn get_metadata_list_size(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
  ) -> Option<usize> {
    let mut result: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_list_size(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(result)
    } else {
      None
    }
  }

  fn get_metadata_list_number(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<f64> {
    let mut value: f64 = 0f64;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_list_number(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        index,
        &mut value as *mut _ as *mut _,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn get_metadata_list_string(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_list_string(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        index,
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_metadata_list_bool(
    &self,
    source: abi::envoy_dynamic_module_type_metadata_source,
    namespace: &str,
    key: &str,
    index: usize,
  ) -> Option<bool> {
    let mut value: bool = false;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_metadata_list_bool(
        self.raw_ptr,
        source,
        str_to_module_buffer(namespace),
        str_to_module_buffer(key),
        index,
        &mut value as *mut _ as *mut _,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn get_filter_state_bytes(&self, key: &[u8]) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_filter_state_bytes(
        self.raw_ptr,
        bytes_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn set_filter_state_bytes(&mut self, key: &[u8], value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_filter_state_bytes(
        self.raw_ptr,
        bytes_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn set_filter_state_typed(&mut self, key: &[u8], value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_filter_state_typed(
        self.raw_ptr,
        bytes_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  fn get_filter_state_typed(&self, key: &[u8]) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_filter_state_typed(
        self.raw_ptr,
        bytes_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_received_request_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedRequestBody,
      )
    };
    if size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedRequestBody,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_buffered_request_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedRequestBody,
      )
    };
    if size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedRequestBody,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_received_request_body_size(&mut self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedRequestBody,
      )
    }
  }

  fn get_buffered_request_body_size(&mut self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedRequestBody,
      )
    }
  }

  fn drain_received_request_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedRequestBody,
        number_of_bytes,
      )
    }
  }

  fn drain_buffered_request_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedRequestBody,
        number_of_bytes,
      )
    }
  }

  fn append_received_request_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedRequestBody,
        bytes_to_module_buffer(data),
      )
    }
  }

  fn append_buffered_request_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedRequestBody,
        bytes_to_module_buffer(data),
      )
    }
  }

  fn get_received_response_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedResponseBody,
      )
    };
    if size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedResponseBody,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_buffered_response_body(&mut self) -> Option<Vec<EnvoyMutBuffer<'_>>> {
    let size = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedResponseBody,
      )
    };
    if size == 0 {
      return None;
    }

    let buffers: Vec<EnvoyMutBuffer> = vec![EnvoyMutBuffer::default(); size];
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_chunks(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedResponseBody,
        buffers.as_ptr() as *mut abi::envoy_dynamic_module_type_envoy_buffer,
      )
    };
    if success {
      Some(buffers)
    } else {
      None
    }
  }

  fn get_received_response_body_size(&mut self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedResponseBody,
      )
    }
  }

  fn get_buffered_response_body_size(&mut self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_http_get_body_size(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedResponseBody,
      )
    }
  }

  fn drain_received_response_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedResponseBody,
        number_of_bytes,
      )
    }
  }

  fn drain_buffered_response_body(&mut self, number_of_bytes: usize) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_drain_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedResponseBody,
        number_of_bytes,
      )
    }
  }

  fn append_received_response_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::ReceivedResponseBody,
        bytes_to_module_buffer(data),
      )
    }
  }

  fn append_buffered_response_body(&mut self, data: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_append_body(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_body_type::BufferedResponseBody,
        bytes_to_module_buffer(data),
      )
    }
  }

  fn received_buffered_request_body(&mut self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_http_received_buffered_request_body(self.raw_ptr) }
  }

  fn received_buffered_response_body(&mut self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_http_received_buffered_response_body(self.raw_ptr) }
  }

  fn clear_route_cache(&mut self) {
    unsafe { abi::envoy_dynamic_module_callback_http_clear_route_cache(self.raw_ptr) }
  }

  fn remove_request_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestHeader,
        str_to_module_buffer(key),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: std::ptr::null_mut(),
          length: 0,
        },
      )
    }
  }

  fn remove_request_trailer(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::RequestTrailer,
        str_to_module_buffer(key),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: std::ptr::null_mut(),
          length: 0,
        },
      )
    }
  }

  fn remove_response_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseHeader,
        str_to_module_buffer(key),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: std::ptr::null_mut(),
          length: 0,
        },
      )
    }
  }

  fn remove_response_trailer(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_header(
        self.raw_ptr,
        abi::envoy_dynamic_module_type_http_header_type::ResponseTrailer,
        str_to_module_buffer(key),
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: std::ptr::null_mut(),
          length: 0,
        },
      )
    }
  }

  fn get_attribute_string(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_get_attribute_string(
        self.raw_ptr,
        attribute_id,
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<i64> {
    let mut result: i64 = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_get_attribute_int(
        self.raw_ptr,
        attribute_id,
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(result)
    } else {
      None
    }
  }

  fn get_attribute_bool(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<bool> {
    let mut result: bool = false;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_get_attribute_bool(
        self.raw_ptr,
        attribute_id,
        &mut result as *mut _ as *mut _,
      )
    };
    if success {
      Some(result)
    } else {
      None
    }
  }

  fn send_http_callout<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();
    let mut callout_id: u64 = 0;

    let result = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_http_callout(
        self.raw_ptr,
        &mut callout_id as *mut _ as *mut _,
        str_to_module_buffer(cluster_name),
        headers_ptr as *const _ as *mut _,
        headers_len,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        timeout_milliseconds,
      )
    };

    (result, callout_id)
  }

  fn start_http_stream<'a>(
    &mut self,
    cluster_name: &'a str,
    headers: &'a [(&'a str, &'a [u8])],
    body: Option<&'a [u8]>,
    end_stream: bool,
    timeout_milliseconds: u64,
  ) -> (abi::envoy_dynamic_module_type_http_callout_init_result, u64) {
    let body_ptr = body.map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
    let body_length = body.map(|s| s.len()).unwrap_or(0);
    let HeaderPairSlice(headers_ptr, headers_len) = headers.into();
    let mut stream_id: u64 = 0;

    let result = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_start_http_stream(
        self.raw_ptr,
        &mut stream_id as *mut _ as *mut _,
        str_to_module_buffer(cluster_name),
        headers_ptr as *const _ as *mut _,
        headers_len,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: body_ptr as *mut _,
          length: body_length,
        },
        end_stream,
        timeout_milliseconds,
      )
    };

    (result, stream_id)
  }

  unsafe fn send_http_stream_data(
    &mut self,
    stream_handle: u64,
    data: &[u8],
    end_stream: bool,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_stream_send_data(
        self.raw_ptr,
        stream_handle,
        bytes_to_module_buffer(data),
        end_stream,
      )
    }
  }

  unsafe fn send_http_stream_trailers<'a>(
    &mut self,
    stream_handle: u64,
    trailers: &'a [(&'a str, &'a [u8])],
  ) -> bool {
    let HeaderPairSlice(trailers_ptr, trailers_len) = trailers.into();
    unsafe {
      abi::envoy_dynamic_module_callback_http_stream_send_trailers(
        self.raw_ptr,
        stream_handle,
        trailers_ptr as *const _ as *mut _,
        trailers_len,
      )
    }
  }

  unsafe fn reset_http_stream(&mut self, stream_handle: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_reset_http_stream(self.raw_ptr, stream_handle);
    }
  }

  fn get_most_specific_route_config(&self) -> Option<std::sync::Arc<dyn Any>> {
    unsafe {
      let filter_config_ptr =
        abi::envoy_dynamic_module_callback_get_most_specific_route_config(self.raw_ptr)
          as *mut std::sync::Arc<dyn Any>;

      filter_config_ptr.as_ref().cloned()
    }
  }

  fn continue_decoding(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_continue_decoding(self.raw_ptr);
    }
  }

  fn continue_encoding(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_continue_encoding(self.raw_ptr);
    }
  }

  fn new_scheduler(&self) -> impl EnvoyHttpFilterScheduler + 'static {
    unsafe {
      let scheduler_ptr =
        abi::envoy_dynamic_module_callback_http_filter_scheduler_new(self.raw_ptr);
      EnvoyHttpFilterSchedulerImpl {
        raw_ptr: scheduler_ptr,
      }
    }
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_counter(
        self.raw_ptr,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyCounterVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_counter(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_gauge(
        self.raw_ptr,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_increment_gauge(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_decrement_gauge(
        self.raw_ptr,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_decrement_gauge(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_set_gauge(
        self.raw_ptr,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyGaugeVecId(id) = id;
    let res = unsafe {
      abi::envoy_dynamic_module_callback_http_filter_set_gauge(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    };
    if res == envoy_dynamic_module_type_metrics_result::Success {
      Ok(())
    } else {
      Err(res)
    }
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramId(id) = id;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_record_histogram_value(
        self.raw_ptr,
        id,
        std::ptr::null_mut(),
        0,
        value,
      )
    })?;
    Ok(())
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    labels: &[&str],
    value: u64,
  ) -> Result<(), envoy_dynamic_module_type_metrics_result> {
    let EnvoyHistogramVecId(id) = id;
    Result::from(unsafe {
      abi::envoy_dynamic_module_callback_http_filter_record_histogram_value(
        self.raw_ptr,
        id,
        labels.as_ptr() as *const _ as *mut _,
        labels.len(),
        value,
      )
    })?;
    Ok(())
  }

  fn get_worker_index(&self) -> u32 {
    unsafe { abi::envoy_dynamic_module_callback_http_filter_get_worker_index(self.raw_ptr) }
  }

  fn set_socket_option_int(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
    value: i64,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_socket_option_int(
        self.raw_ptr,
        level,
        name,
        state,
        direction,
        value,
      )
    }
  }

  fn set_socket_option_bytes(
    &mut self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
    value: &[u8],
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_socket_option_bytes(
        self.raw_ptr,
        level,
        name,
        state,
        direction,
        abi::envoy_dynamic_module_type_module_buffer {
          ptr: value.as_ptr() as *const _,
          length: value.len(),
        },
      )
    }
  }

  fn get_socket_option_int(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
  ) -> Option<i64> {
    let mut value: i64 = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_socket_option_int(
        self.raw_ptr,
        level,
        name,
        state,
        direction,
        &mut value,
      )
    };
    if success {
      Some(value)
    } else {
      None
    }
  }

  fn get_socket_option_bytes(
    &self,
    level: i64,
    name: i64,
    state: abi::envoy_dynamic_module_type_socket_option_state,
    direction: abi::envoy_dynamic_module_type_socket_direction,
  ) -> Option<Vec<u8>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_socket_option_bytes(
        self.raw_ptr,
        level,
        name,
        state,
        direction,
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      let slice = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(result.ptr as *const u8, result.length)
      };
      Some(slice.to_vec())
    } else {
      None
    }
  }

  fn get_buffer_limit(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_http_get_buffer_limit(self.raw_ptr) }
  }

  fn set_buffer_limit(&mut self, limit: u64) {
    unsafe { abi::envoy_dynamic_module_callback_http_set_buffer_limit(self.raw_ptr, limit) }
  }

  fn get_active_span<'a>(&'a self) -> Option<Box<dyn EnvoySpan + 'a>> {
    let raw_ptr = unsafe { abi::envoy_dynamic_module_callback_http_get_active_span(self.raw_ptr) };
    if raw_ptr.is_null() {
      None
    } else {
      Some(Box::new(EnvoySpanImpl {
        raw_ptr,
        filter_ptr: self.raw_ptr,
      }))
    }
  }

  fn spawn_child_span<'a>(&'a self, operation_name: &str) -> Option<Box<dyn EnvoyChildSpan + 'a>> {
    // Get the active span pointer directly.
    let span_ptr = unsafe { abi::envoy_dynamic_module_callback_http_get_active_span(self.raw_ptr) };
    if span_ptr.is_null() {
      return None;
    }
    // Spawn the child span directly from the raw pointer.
    let raw_ptr = unsafe {
      abi::envoy_dynamic_module_callback_http_span_spawn_child(
        self.raw_ptr,
        span_ptr,
        str_to_module_buffer(operation_name),
      )
    };
    if raw_ptr.is_null() {
      None
    } else {
      Some(Box::new(EnvoyChildSpanImpl {
        raw_ptr,
        filter_ptr: self.raw_ptr,
        finished: false,
      }))
    }
  }

  fn get_cluster_name(&self) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_cluster_name(self.raw_ptr, &mut result as *mut _)
    };
    if success && !result.ptr.is_null() {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }

  fn get_cluster_host_count(&self, priority: u32) -> Option<ClusterHostCount> {
    let mut total: usize = 0;
    let mut healthy: usize = 0;
    let mut degraded: usize = 0;
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_cluster_host_count(
        self.raw_ptr,
        priority,
        &mut total as *mut _,
        &mut healthy as *mut _,
        &mut degraded as *mut _,
      )
    };
    if success {
      Some(ClusterHostCount {
        total,
        healthy,
        degraded,
      })
    } else {
      None
    }
  }

  fn set_upstream_override_host(&mut self, host: &str, strict: bool) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_http_set_upstream_override_host(
        self.raw_ptr,
        str_to_module_buffer(host),
        strict,
      )
    }
  }

  fn reset_stream(
    &mut self,
    reason: abi::envoy_dynamic_module_type_http_filter_stream_reset_reason,
    details: &str,
  ) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_reset_stream(
        self.raw_ptr,
        reason,
        str_to_module_buffer(details),
      )
    }
  }

  fn send_go_away_and_close(&mut self, graceful: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_send_go_away_and_close(self.raw_ptr, graceful)
    }
  }

  fn recreate_stream<'a>(&mut self, headers: Option<&'a [(&'a str, &'a [u8])]>) -> bool {
    match headers {
      Some(headers) => {
        let HeaderPairSlice(headers_ptr, headers_len) = headers.into();
        unsafe {
          abi::envoy_dynamic_module_callback_http_filter_recreate_stream(
            self.raw_ptr,
            headers_ptr as *mut _,
            headers_len,
          )
        }
      },
      None => unsafe {
        abi::envoy_dynamic_module_callback_http_filter_recreate_stream(
          self.raw_ptr,
          std::ptr::null_mut(),
          0,
        )
      },
    }
  }

  fn clear_route_cluster_cache(&mut self) {
    unsafe { abi::envoy_dynamic_module_callback_http_clear_route_cluster_cache(self.raw_ptr) }
  }
}

impl EnvoyHttpFilterImpl {
  pub(crate) fn new(raw_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr) -> Self {
    Self { raw_ptr }
  }

  /// Implement the common logic for getting all headers/trailers.
  fn get_headers_impl(
    &self,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let count = unsafe {
      abi::envoy_dynamic_module_callback_http_get_headers_size(self.raw_ptr, header_type)
    };
    if count == 0 {
      return Vec::default();
    }

    let mut headers: Vec<(EnvoyBuffer, EnvoyBuffer)> = Vec::with_capacity(count);
    let success = unsafe {
      abi::envoy_dynamic_module_callback_http_get_headers(
        self.raw_ptr,
        header_type,
        headers.as_mut_ptr() as *mut abi::envoy_dynamic_module_type_envoy_http_header,
      )
    };
    unsafe {
      headers.set_len(count);
    }
    if success {
      headers
    } else {
      Vec::default()
    }
  }

  /// This implements the common logic for getting the header/trailer values.
  fn get_header_value_impl(
    &self,
    key: &str,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };

    unsafe {
      abi::envoy_dynamic_module_callback_http_get_header(
        self.raw_ptr,
        header_type,
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
        0, // Only the first value is needed.
        std::ptr::null_mut(),
      )
    };

    if result.ptr.is_null() {
      None
    } else {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    }
  }

  /// This implements the common logic for getting the header/trailer values.
  ///
  /// TODO: use smallvec or similar to avoid the heap allocations for majority of the cases.
  fn get_header_values_impl(
    &self,
    key: &str,
    header_type: abi::envoy_dynamic_module_type_http_header_type,
  ) -> Vec<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };

    let mut count: usize = 0;

    // Get the first value to get the count.
    let ret = unsafe {
      abi::envoy_dynamic_module_callback_http_get_header(
        self.raw_ptr,
        header_type,
        str_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
        0,
        &mut count as *mut _ as *mut _,
      )
    };

    let mut results = Vec::new();
    if count == 0 || !ret {
      return results;
    }

    // At this point, we assume at least one value is present.
    results.push(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) });
    // So, we iterate from 1 to count - 1.
    for i in 1..count {
      let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: std::ptr::null(),
        length: 0,
      };
      unsafe {
        abi::envoy_dynamic_module_callback_http_get_header(
          self.raw_ptr,
          header_type,
          str_to_module_buffer(key),
          &mut result as *mut _ as *mut _,
          i,
          std::ptr::null_mut(),
        )
      };
      // Within the range, all results are guaranteed to be non-null by Envoy.
      results.push(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) });
    }
    results
  }
}

/// This represents a thin thread-safe object that can be used to schedule a generic event to the
/// Envoy HTTP filter on the work thread.
///
/// For example, this can be used to offload some blocking work from the HTTP filter processing
/// thread to a module-managed thread, and then schedule an event to continue
/// processing the request.
///
/// Since this is primarily designed to be used from a different thread than the one
/// where the [`HttpFilter`] instance was created, it is marked as `Send` so that
/// the [`Box<dyn EnvoyHttpFilterScheduler>`] can be sent across threads.
///
/// It is also safe to be called concurrently, so it is marked as `Sync` as well.
#[automock]
pub trait EnvoyHttpFilterScheduler: Send + Sync {
  /// Commit the scheduled event to the worker thread where [`HttpFilter`] is running.
  ///
  /// It accepts an `event_id` which can be used to distinguish different events
  /// scheduled by the same filter. The `event_id` can be any value.
  ///
  /// Once this is called, [`HttpFilter::on_scheduled`] will be called with
  /// the same `event_id` on the worker thread where the filter is running IF
  /// by the time the event is committed, the filter is still alive.
  fn commit(&self, event_id: u64);
}

/// This implements the [`EnvoyHttpFilterScheduler`] trait with the given raw pointer to the Envoy
/// HTTP filter scheduler object.
struct EnvoyHttpFilterSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_scheduler_module_ptr,
}

unsafe impl Send for EnvoyHttpFilterSchedulerImpl {}
unsafe impl Sync for EnvoyHttpFilterSchedulerImpl {}

impl Drop for EnvoyHttpFilterSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyHttpFilterScheduler for EnvoyHttpFilterSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_scheduler_commit(self.raw_ptr, event_id);
    }
  }
}

// Box<dyn EnvoyHttpFilterScheduler> is returned by mockall, so we need to implement
// EnvoyHttpFilterScheduler for it as well.
impl EnvoyHttpFilterScheduler for Box<dyn EnvoyHttpFilterScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// This represents a thread-safe object that can be used to schedule a generic event to the
/// Envoy HTTP filter config on the main thread.
#[automock]
pub trait EnvoyHttpFilterConfigScheduler: Send + Sync {
  /// Commit the scheduled event to the main thread.
  fn commit(&self, event_id: u64);
}

struct EnvoyHttpFilterConfigSchedulerImpl {
  raw_ptr: abi::envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr,
}

unsafe impl Send for EnvoyHttpFilterConfigSchedulerImpl {}
unsafe impl Sync for EnvoyHttpFilterConfigSchedulerImpl {}

impl Drop for EnvoyHttpFilterConfigSchedulerImpl {
  fn drop(&mut self) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_scheduler_delete(self.raw_ptr);
    }
  }
}

impl EnvoyHttpFilterConfigScheduler for EnvoyHttpFilterConfigSchedulerImpl {
  fn commit(&self, event_id: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_http_filter_config_scheduler_commit(
        self.raw_ptr,
        event_id,
      );
    }
  }
}

impl EnvoyHttpFilterConfigScheduler for Box<dyn EnvoyHttpFilterConfigScheduler> {
  fn commit(&self, event_id: u64) {
    (**self).commit(event_id);
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_new(
  envoy_filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    // The name is sourced from a protobuf string field (and thus UTF-8 by contract); we still
    // route through `str_lossy_from_raw` so a malformed input on the FFI seam produces a lossy
    // decode rather than undefined behaviour.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    let mut envoy_filter_config = EnvoyHttpFilterConfigImpl {
      raw_ptr: envoy_filter_config_ptr,
    };

    envoy_dynamic_module_on_http_filter_config_new_impl(
      &mut envoy_filter_config,
      name_str.as_ref(),
      config_slice,
      NEW_HTTP_FILTER_CONFIG_FUNCTION
        .get()
        .expect("NEW_HTTP_FILTER_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_config_new", panic);
    std::ptr::null()
  })
}

pub fn envoy_dynamic_module_on_http_filter_config_new_impl(
  envoy_filter_config: &mut EnvoyHttpFilterConfigImpl,
  name: &str,
  config: &[u8],
  new_fn: &NewHttpFilterConfigFunction<EnvoyHttpFilterConfigImpl, EnvoyHttpFilterImpl>,
) -> abi::envoy_dynamic_module_type_http_filter_config_module_ptr {
  if let Some(config) = new_fn(envoy_filter_config, name, config) {
    crate::wrap_into_c_void_ptr!(config)
  } else {
    std::ptr::null()
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(config_ptr, HttpFilterConfig<EnvoyHttpFilterImpl>);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_config_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_scheduled(
  _envoy_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    config.on_scheduled(event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_scheduled",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_per_route_config_new(
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    // See `envoy_dynamic_module_on_http_filter_config_new`: route through `str_lossy_from_raw`
    // so a malformed input on the FFI seam produces a lossy decode rather than undefined
    // behaviour.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    envoy_dynamic_module_on_http_filter_per_route_config_new_impl(
      name_str.as_ref(),
      config_slice,
      NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION
        .get()
        .expect("NEW_HTTP_FILTER_PER_ROUTE_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_per_route_config_new",
      panic,
    );
    std::ptr::null()
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_per_route_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let ptr = config_ptr as *mut std::sync::Arc<dyn Any>;
    std::mem::drop(Box::from_raw(ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_per_route_config_destroy",
      panic,
    );
  });
}

pub fn envoy_dynamic_module_on_http_filter_per_route_config_new_impl(
  name: &str,
  config: &[u8],
  new_fn: &NewHttpFilterPerRouteConfigFunction,
) -> abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr {
  if let Some(config) = new_fn(name, config) {
    let arc: std::sync::Arc<dyn Any> = std::sync::Arc::from(config);
    let ptr = Box::into_raw(Box::new(arc));
    ptr as abi::envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
  } else {
    std::ptr::null()
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_new(
  filter_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  filter_envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let mut envoy_filter = EnvoyHttpFilterImpl {
      raw_ptr: filter_envoy_ptr,
    };
    let filter_config = {
      let raw = filter_config_ptr as *const *const dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
      &**raw
    };
    envoy_dynamic_module_on_http_filter_new_impl(&mut envoy_filter, filter_config)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_new", panic);
    std::ptr::null()
  })
}

pub fn envoy_dynamic_module_on_http_filter_new_impl(
  envoy_filter: &mut EnvoyHttpFilterImpl,
  filter_config: &dyn HttpFilterConfig<EnvoyHttpFilterImpl>,
) -> abi::envoy_dynamic_module_type_http_filter_module_ptr {
  let filter = filter_config.new_http_filter(envoy_filter);
  crate::wrap_into_c_void_ptr!(filter)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_destroy(
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    crate::drop_wrapped_c_void_ptr!(filter_ptr, HttpFilter<EnvoyHttpFilterImpl>);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_destroy", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_stream_complete(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_stream_complete(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_stream_complete", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_headers_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_request_headers(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_request_headers", panic);
    // Fail-closed: stop iteration so the request never reaches upstream after a panic.
    abi::envoy_dynamic_module_type_on_http_filter_request_headers_status::StopIteration
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_body_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_request_body(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_request_body", panic);
    // Fail-closed: stop iteration without buffering further data.
    abi::envoy_dynamic_module_type_on_http_filter_request_body_status::StopIterationNoBuffer
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_request_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_request_trailers(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_request_trailers",
      panic,
    );
    abi::envoy_dynamic_module_type_on_http_filter_request_trailers_status::StopIteration
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_headers_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_response_headers(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_response_headers",
      panic,
    );
    abi::envoy_dynamic_module_type_on_http_filter_response_headers_status::StopIteration
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_body(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  end_of_stream: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_body_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_response_body(&mut EnvoyHttpFilterImpl::new(envoy_ptr), end_of_stream)
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_response_body", panic);
    abi::envoy_dynamic_module_type_on_http_filter_response_body_status::StopIterationNoBuffer
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_response_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) -> abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_response_trailers(&mut EnvoyHttpFilterImpl::new(envoy_ptr))
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_response_trailers",
      panic,
    );
    abi::envoy_dynamic_module_type_on_http_filter_response_trailers_status::StopIteration
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_callout_done(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    let headers = if headers_size > 0 {
      Some(unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          headers as *const (EnvoyBuffer, EnvoyBuffer),
          headers_size,
        )
      })
    } else {
      None
    };
    let body = if body_chunks_size > 0 {
      Some(unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          body_chunks as *const EnvoyBuffer,
          body_chunks_size,
        )
      })
    } else {
      None
    };
    filter.on_http_callout_done(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      callout_id,
      result,
      headers,
      body,
    )
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_callout_done",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_scheduled(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  event_id: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_scheduled(&mut EnvoyHttpFilterImpl::new(envoy_ptr), event_id);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_scheduled", panic);
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter
      .on_downstream_above_write_buffer_high_watermark(&mut EnvoyHttpFilterImpl::new(envoy_ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_downstream_below_write_buffer_low_watermark(&mut EnvoyHttpFilterImpl::new(envoy_ptr));
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_local_reply(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  response_code: u32,
  details: abi::envoy_dynamic_module_type_envoy_buffer,
  reset_imminent: bool,
) -> abi::envoy_dynamic_module_type_on_http_filter_local_reply_status {
  catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    let details_buffer = EnvoyBuffer::new_from_raw(details.ptr as *const u8, details.length);
    filter.on_local_reply(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      response_code,
      details_buffer,
      reset_imminent,
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_http_filter_local_reply", panic);
    // Continue with the planned local reply; do not escalate to a stream reset on panic.
    abi::envoy_dynamic_module_type_on_http_filter_local_reply_status::Continue
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_stream_headers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  stream_handle: u64,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  end_stream: bool,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    let headers = if headers_size > 0 {
      unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          headers as *const (EnvoyBuffer, EnvoyBuffer),
          headers_size,
        )
      }
    } else {
      &[]
    };
    filter.on_http_stream_headers(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      stream_handle,
      headers,
      end_stream,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_stream_headers",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_stream_data(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  stream_handle: u64,
  data: *const abi::envoy_dynamic_module_type_envoy_buffer,
  data_count: usize,
  end_stream: bool,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    let data = if data_count > 0 {
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(data as *const EnvoyBuffer, data_count) }
    } else {
      &[]
    };
    filter.on_http_stream_data(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      stream_handle,
      data,
      end_stream,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_stream_data",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_stream_trailers(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  stream_handle: u64,
  trailers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  trailers_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    let trailers = if trailers_size > 0 {
      unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          trailers as *const (EnvoyBuffer, EnvoyBuffer),
          trailers_size,
        )
      }
    } else {
      &[]
    };
    filter.on_http_stream_trailers(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      stream_handle,
      trailers,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_stream_trailers",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_stream_complete(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  stream_handle: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_http_stream_complete(&mut EnvoyHttpFilterImpl::new(envoy_ptr), stream_handle);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_stream_complete",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_http_stream_reset(
  envoy_ptr: abi::envoy_dynamic_module_type_http_filter_envoy_ptr,
  filter_ptr: abi::envoy_dynamic_module_type_http_filter_module_ptr,
  stream_handle: u64,
  reset_reason: abi::envoy_dynamic_module_type_http_stream_reset_reason,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let filter = filter_ptr as *mut *mut dyn HttpFilter<EnvoyHttpFilterImpl>;
    let filter = &mut **filter;
    filter.on_http_stream_reset(
      &mut EnvoyHttpFilterImpl::new(envoy_ptr),
      stream_handle,
      reset_reason,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_http_stream_reset",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_callout_done(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  callout_id: u64,
  result: abi::envoy_dynamic_module_type_http_callout_result,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  body_chunks: *const abi::envoy_dynamic_module_type_envoy_buffer,
  body_chunks_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    let headers = if headers_size > 0 {
      Some(unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          headers as *const (EnvoyBuffer, EnvoyBuffer),
          headers_size,
        )
      })
    } else {
      None
    };
    let body = if body_chunks_size > 0 {
      Some(unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          body_chunks as *const EnvoyBuffer,
          body_chunks_size,
        )
      })
    } else {
      None
    };
    config.on_http_callout_done(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      callout_id,
      result,
      headers,
      body,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_callout_done",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_stream_headers(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  stream_handle: u64,
  headers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  headers_size: usize,
  end_stream: bool,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    let headers = if headers_size > 0 {
      unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          headers as *const (EnvoyBuffer, EnvoyBuffer),
          headers_size,
        )
      }
    } else {
      &[]
    };
    config.on_http_stream_headers(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      stream_handle,
      headers,
      end_stream,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_stream_headers",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_stream_data(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  stream_handle: u64,
  data: *const abi::envoy_dynamic_module_type_envoy_buffer,
  data_count: usize,
  end_stream: bool,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    let data = if data_count > 0 {
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(data as *const EnvoyBuffer, data_count) }
    } else {
      &[]
    };
    config.on_http_stream_data(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      stream_handle,
      data,
      end_stream,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_stream_data",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_stream_trailers(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  stream_handle: u64,
  trailers: *const abi::envoy_dynamic_module_type_envoy_http_header,
  trailers_size: usize,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    let trailers = if trailers_size > 0 {
      unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(
          trailers as *const (EnvoyBuffer, EnvoyBuffer),
          trailers_size,
        )
      }
    } else {
      &[]
    };
    config.on_http_stream_trailers(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      stream_handle,
      trailers,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_stream_trailers",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_stream_complete(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  stream_handle: u64,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    config.on_http_stream_complete(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      stream_handle,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_stream_complete",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_http_filter_config_http_stream_reset(
  envoy_config_ptr: abi::envoy_dynamic_module_type_http_filter_config_envoy_ptr,
  config_ptr: abi::envoy_dynamic_module_type_http_filter_config_module_ptr,
  stream_handle: u64,
  reset_reason: abi::envoy_dynamic_module_type_http_stream_reset_reason,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    let config = config_ptr as *mut *mut dyn HttpFilterConfig<EnvoyHttpFilterImpl>;
    let config = &**config;
    config.on_http_stream_reset(
      &mut EnvoyHttpFilterConfigImpl {
        raw_ptr: envoy_config_ptr,
      },
      stream_handle,
      reset_reason,
    );
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_http_filter_config_http_stream_reset",
      panic,
    );
  });
}
