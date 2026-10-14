//! Route specifier support for dynamic modules.
//!
//! This module provides the types a dynamic module uses to decide the route of a request. The
//! entry point is the `route_specifier:` arm of [`crate::declare_all_init_functions!`], which
//! registers a factory through [`crate::NEW_ROUTE_SPECIFIER_CONFIG_FUNCTION`] and lets a single
//! module dispatch by `specifier_name`.

pub use crate::cluster_specifier::ResourcePriority;
use crate::{
  abi, ffi_export, ClusterHostCount, EnvoyBuffer, EnvoyCounterId, EnvoyCounterVecId, EnvoyGaugeId,
  EnvoyGaugeVecId, EnvoyHistogramId, EnvoyHistogramVecId,
};
#[cfg(any(test, feature = "mock"))]
use mockall::*;
use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::ptr;
use std::sync::Arc;
use std::time::Duration;

/// The decision a route specifier records for a request with
/// [`RouteSpecifierContext::set_decision`]. It selects how Envoy builds the route.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteDecision {
  /// The default behavior, in effect when the module records no decision: Envoy generates a new
  /// route based on what the setter methods of [`RouteSpecifierContext`] recorded. The base is
  /// the template recorded with [`RouteSpecifierContext::select_template`], evaluated against
  /// the request like a configured route, or the route the specifier was given when no template
  /// was recorded, and the recorded overrides are applied on top of it. With nothing recorded
  /// the route the specifier was given is used unchanged.
  Unspecified,
  /// Use the route the specifier was given, unchanged. The recorded template and overrides are
  /// ignored.
  PassThrough,
  /// Use no route, so the request is handled as if nothing had matched.
  NoRoute,
  /// The module could not reach a decision, so Envoy applies the configured failure policy.
  Error,
  /// Use the previous route of the stream unchanged, without building a new route. Valid only when
  /// a previous route exists, otherwise Envoy applies the failure policy.
  ReusePrevious,
}

impl RouteDecision {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_specifier_decision {
    match self {
      Self::Unspecified => abi::envoy_dynamic_module_type_route_specifier_decision::Unspecified,
      Self::PassThrough => abi::envoy_dynamic_module_type_route_specifier_decision::PassThrough,
      Self::NoRoute => abi::envoy_dynamic_module_type_route_specifier_decision::NoRoute,
      Self::Error => abi::envoy_dynamic_module_type_route_specifier_decision::Error,
      Self::ReusePrevious => abi::envoy_dynamic_module_type_route_specifier_decision::ReusePrevious,
    }
  }
}

/// The status a route specifier returns from [`RouteSpecifierConfig::on_route`]: whether the
/// route specifiers configured after this one run for the request, and whether route matching
/// accepts the route the decision produced.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OnRouteStatus {
  /// The chain continues: the route the decision produced is handed to the next specifier.
  Continue,
  /// The chain stops and the route the decision produced is the final route.
  StopIteration,
  /// The chain stops, the route is turned down, and route matching carries on with the next route
  /// of the list being evaluated. The recorded decision, template and overrides are ignored.
  StopIterationAndSkipRoute,
}

impl OnRouteStatus {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_specifier_on_route_status {
    match self {
      Self::Continue => abi::envoy_dynamic_module_type_route_specifier_on_route_status::Continue,
      Self::StopIteration => {
        abi::envoy_dynamic_module_type_route_specifier_on_route_status::StopIteration
      },
      Self::StopIterationAndSkipRoute => {
        abi::envoy_dynamic_module_type_route_specifier_on_route_status::StopIterationAndSkipRoute
      },
    }
  }
}

/// The kind of a route.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteKind {
  /// There is no route.
  None,
  /// The route forwards the request to a cluster.
  RouteEntry,
  /// The route answers the request directly, either with a redirect or with a direct response.
  DirectResponse,
}

impl RouteKind {
  fn from_abi(value: abi::envoy_dynamic_module_type_route_specifier_route_kind) -> Self {
    match value {
      abi::envoy_dynamic_module_type_route_specifier_route_kind::RouteEntry => Self::RouteEntry,
      abi::envoy_dynamic_module_type_route_specifier_route_kind::DirectResponse => {
        Self::DirectResponse
      },
      _ => Self::None,
    }
  }
}

/// How a header a module adds combines with a header of the same name that is already present.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HeaderAppendAction {
  /// Append the value, or add the header when it is absent.
  AppendIfExistsOrAdd,
  /// Add the header only when it is absent.
  AddIfAbsent,
  /// Replace the value, or add the header when it is absent.
  OverwriteIfExistsOrAdd,
  /// Replace the value only when the header is present.
  OverwriteIfExists,
}

impl HeaderAppendAction {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_specifier_header_append_action {
    match self {
      Self::AppendIfExistsOrAdd => {
        abi::envoy_dynamic_module_type_route_specifier_header_append_action::AppendIfExistsOrAdd
      },
      Self::AddIfAbsent => {
        abi::envoy_dynamic_module_type_route_specifier_header_append_action::AddIfAbsent
      },
      Self::OverwriteIfExistsOrAdd => {
        abi::envoy_dynamic_module_type_route_specifier_header_append_action::OverwriteIfExistsOrAdd
      },
      Self::OverwriteIfExists => {
        abi::envoy_dynamic_module_type_route_specifier_header_append_action::OverwriteIfExists
      },
    }
  }
}

/// Convert a duration to the millisecond count the ABI takes, saturating instead of wrapping.
fn duration_to_millis(duration: Duration) -> u64 {
  u64::try_from(duration.as_millis()).unwrap_or(u64::MAX)
}

/// The properties of the route the module is resolving that are free to read.
///
/// A property the kind of the route does not carry is absent. The route entry properties are absent
/// for a route that answers the request directly, and `response_code` is absent for a route entry.
/// After a template is selected these are the properties of the template.
#[derive(Debug)]
pub struct InputRoute<'a> {
  /// The kind of the route.
  pub kind: RouteKind,
  /// The name of the route.
  pub name: EnvoyBuffer<'a>,
  /// The name of the virtual host the route belongs to.
  pub virtual_host_name: EnvoyBuffer<'a>,
  /// Whether the route carries metadata, read per namespace and key with the metadata getters.
  pub has_metadata: bool,
  /// The upstream cluster of the route.
  pub cluster_name: Option<EnvoyBuffer<'a>>,
  /// The route timeout.
  pub timeout: Option<Duration>,
  /// The stream idle timeout.
  pub idle_timeout: Option<Duration>,
  /// The maximum stream duration.
  pub max_stream_duration: Option<Duration>,
  /// The upstream resource priority.
  pub priority: Option<ResourcePriority>,
  /// The request body buffer limit in bytes.
  pub request_body_buffer_limit: Option<u64>,
  /// The status code Envoy replies with when the selected cluster does not exist.
  pub cluster_not_found_response_code: Option<u32>,
  /// Whether the route configures subset load balancing metadata match criteria.
  pub has_metadata_match: bool,
  /// Whether the route configures a hash policy.
  pub has_hash_policy: bool,
  /// Whether the route configures any rate limit policy.
  pub has_rate_limits: bool,
  /// The number of request mirror policies the route configures.
  pub request_mirror_policies_count: usize,
  /// The status code the route answers the request with.
  pub response_code: Option<u32>,
}

/// Why filling the request headers into a buffer failed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FillError {
  /// The request header map was not available.
  Unavailable,
}

/// A read only view over the request headers filled into a caller owned buffer.
///
/// The buffer holds the ABI header structs and the view borrows both it and the context, so a view
/// cannot outlive either. Each entry yields a key and value [`EnvoyBuffer`] that point into Envoy
/// owned request memory.
pub struct HeaderView<'a> {
  headers: &'a [MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>],
}

impl<'a> HeaderView<'a> {
  /// The number of headers.
  pub fn len(&self) -> usize {
    self.headers.len()
  }

  /// Whether there are no headers.
  pub fn is_empty(&self) -> bool {
    self.headers.is_empty()
  }

  /// The key and value at the given index, or `None` when it is out of range.
  pub fn get(&self, index: usize) -> Option<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)> {
    self.headers.get(index).map(header_pair)
  }

  /// Iterates the headers as key and value [`EnvoyBuffer`] pairs.
  pub fn iter(&self) -> impl Iterator<Item = (EnvoyBuffer<'a>, EnvoyBuffer<'a>)> + '_ {
    self.headers.iter().map(header_pair)
  }
}

// Builds the key and value pair of a filled header entry field by field, never reinterpreting the
// Rust type as the C struct. The buffers point into Envoy owned request memory.
fn header_pair<'a>(
  entry: &MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>,
) -> (EnvoyBuffer<'a>, EnvoyBuffer<'a>) {
  // Safety: Envoy initialized every entry in the filled range during the fill callback.
  let header = unsafe { entry.assume_init_ref() };
  unsafe {
    (
      EnvoyBuffer::new_from_raw(header.key_ptr as *const u8, header.key_length),
      EnvoyBuffer::new_from_raw(header.value_ptr as *const u8, header.value_length),
    )
  }
}

/// Context for a single route decision.
///
/// It provides read access to the request, to the stream info and to the route that route matching
/// resolved, and the setters that record the decision. A context is valid only for the duration of
/// a single [`RouteSpecifierConfig::on_route`] call and must not be stored.
pub struct RouteSpecifierContext {
  envoy_ptr: *mut c_void,
}

impl RouteSpecifierContext {
  /// Create a new RouteSpecifierContext. Used internally by the SDK.
  ///
  /// # Safety
  ///
  /// `envoy_ptr` must be the route decision context Envoy passed to
  /// [`envoy_dynamic_module_on_route_specifier_on_route`], and the returned value must not outlive
  /// that call.
  #[doc(hidden)]
  pub unsafe fn new(envoy_ptr: *mut c_void) -> Self {
    Self { envoy_ptr }
  }

  /// Get the number of request headers.
  pub fn get_request_headers_count(&self) -> usize {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_request_headers_size(self.envoy_ptr)
    }
  }

  // Fills buf with the request headers. On success buf holds the header count and every entry is
  // initialized. The size and fill callbacks are paired by the shared helper, so the buffer always
  // has room for what Envoy writes. Returns Unavailable when the header map is absent, which never
  // happens during on_route but is handled for safety.
  fn fill_request_headers(
    &self,
    buf: &mut Vec<MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>>,
  ) -> Result<(), FillError> {
    crate::utility::fill_headers(
      buf,
      || self.get_request_headers_count(),
      |headers| unsafe {
        abi::envoy_dynamic_module_callback_route_specifier_get_request_headers(
          self.envoy_ptr,
          headers,
        )
      },
    )
    .map(|_| ())
    .ok_or(FillError::Unavailable)
  }

  /// Fills the caller owned buffer with the request headers and returns a [`HeaderView`] over it.
  ///
  /// The buffer is reused across calls so a module that resolves many routes allocates once. The
  /// returned view borrows both the buffer and the context.
  pub fn get_request_headers_into<'a>(
    &'a self,
    buf: &'a mut Vec<MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>>,
  ) -> Result<HeaderView<'a>, FillError> {
    self.fill_request_headers(buf)?;
    let headers: &'a [MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>] = buf;
    Ok(HeaderView { headers })
  }

  /// Get all request headers as key and value [`EnvoyBuffer`] pairs.
  ///
  /// Returns an empty vector when there are no headers, and an error when the header map is absent,
  /// so a caller cannot confuse a failed fill with an empty one.
  pub fn get_all_request_headers(
    &self,
  ) -> Result<Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)>, FillError> {
    let mut buf: Vec<MaybeUninit<abi::envoy_dynamic_module_type_envoy_http_header>> = Vec::new();
    self.fill_request_headers(&mut buf)?;
    // The pairs point into Envoy owned request memory, so they outlive the local buffer.
    Ok(buf.iter().map(header_pair).collect())
  }

  /// Get the first value of the request header with the given key.
  pub fn get_request_header(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    self
      .get_request_header_value(key, 0)
      .map(|(value, _)| value)
  }

  /// Get the request header value with the given key at the given value index.
  ///
  /// Since a header key can have multiple values, the `index` parameter selects a specific value.
  /// Returns `Some((value, total_count))` where `total_count` is the number of values for the key,
  /// or `None` if the header was not found at the given index.
  pub fn get_request_header_value(
    &self,
    key: &str,
    index: usize,
  ) -> Option<(EnvoyBuffer<'_>, usize)> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let mut total_count: usize = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_request_header_value(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        &mut result,
        index,
        &mut total_count,
      )
    } {
      Some((
        unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) },
        total_count,
      ))
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as a string.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_string(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_attribute_string(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as an integer.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_int(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<u64> {
    let mut result: u64 = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_attribute_int(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get the value of the attribute with the given ID as a boolean.
  ///
  /// If the attribute is not found, not supported or is the wrong type, this returns `None`.
  pub fn get_attribute_bool(
    &self,
    attribute_id: abi::envoy_dynamic_module_type_attribute_id,
  ) -> Option<bool> {
    let mut result = false;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_attribute_bool(
        self.envoy_ptr,
        attribute_id,
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get the string value of a dynamic metadata entry of the stream.
  pub fn get_dynamic_metadata_string(
    &self,
    filter_name: &str,
    path: &str,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata(
        self.envoy_ptr,
        crate::str_to_module_buffer(filter_name),
        crate::str_to_module_buffer(path),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the number value of a dynamic metadata entry of the stream.
  pub fn get_dynamic_metadata_number(&self, filter_name: &str, path: &str) -> Option<f64> {
    let mut result: f64 = 0.0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata_number(
        self.envoy_ptr,
        crate::str_to_module_buffer(filter_name),
        crate::str_to_module_buffer(path),
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get the boolean value of a dynamic metadata entry of the stream.
  pub fn get_dynamic_metadata_bool(&self, filter_name: &str, path: &str) -> Option<bool> {
    let mut result = false;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_dynamic_metadata_bool(
        self.envoy_ptr,
        crate::str_to_module_buffer(filter_name),
        crate::str_to_module_buffer(path),
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Get a filter state value by key.
  ///
  /// Only values an HTTP filter stored as bytes are readable. Together with the dynamic metadata
  /// getters this is how a decision an earlier filter made asynchronously reaches the module when
  /// the route is recomputed.
  pub fn get_filter_state_bytes(&self, key: &str) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_filter_state_bytes(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the stable random value Envoy generated for the request.
  pub fn get_random_value(&self) -> u64 {
    unsafe { abi::envoy_dynamic_module_callback_route_specifier_get_random_value(self.envoy_ptr) }
  }

  /// Get the host counts of a cluster at the given priority level.
  ///
  /// Returns `None` when the cluster is not routable from the current worker.
  pub fn get_cluster_host_count(
    &self,
    cluster_name: &str,
    priority: u32,
  ) -> Option<ClusterHostCount> {
    let mut total: usize = 0;
    let mut healthy: usize = 0;
    let mut degraded: usize = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_cluster_host_count(
        self.envoy_ptr,
        crate::str_to_module_buffer(cluster_name),
        priority,
        &mut total,
        &mut healthy,
        &mut degraded,
      )
    } {
      Some(ClusterHostCount {
        total,
        healthy,
        degraded,
      })
    } else {
      None
    }
  }

  /// Get the properties of the route the module is resolving that are free to read, in one call.
  /// After a template is selected with [`RouteSpecifierContext::select_template`] these are the
  /// properties of the template.
  ///
  /// Prefer this over the individual getters when more than one of them is read. Returns `None`
  /// when no route was resolved for the request.
  pub fn input_route(&self) -> Option<InputRoute<'_>> {
    self.read_route(abi::envoy_dynamic_module_callback_route_specifier_get_input_route)
  }

  /// Get the properties of the previous route of the stream, the route the connection manager last
  /// installed before this resolution, in one call.
  ///
  /// It is `None` on the first resolution, after an internal redirect that recreated the stream,
  /// and when a filter installed a null route. It can be a static route, a route of another
  /// specifier, a filter supplied route or a route this specifier produced.
  pub fn previous_route(&self) -> Option<InputRoute<'_>> {
    self.read_route(abi::envoy_dynamic_module_callback_route_specifier_get_previous_route)
  }

  // Reads a route through the given getter and builds an InputRoute, shared by input_route and
  // previous_route which differ only in which route Envoy reads.
  fn read_route(
    &self,
    callback: unsafe extern "C" fn(
      *mut c_void,
      *mut abi::envoy_dynamic_module_type_route_specifier_input_route,
    ) -> bool,
  ) -> Option<InputRoute<'_>> {
    let null_buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let mut result = abi::envoy_dynamic_module_type_route_specifier_input_route {
      kind: abi::envoy_dynamic_module_type_route_specifier_route_kind::None,
      name: null_buffer,
      virtual_host_name: null_buffer,
      has_metadata: false,
      cluster_name: null_buffer,
      timeout_ms: 0,
      has_idle_timeout: false,
      idle_timeout_ms: 0,
      has_max_stream_duration: false,
      max_stream_duration_ms: 0,
      priority: abi::envoy_dynamic_module_type_resource_priority::Default,
      request_body_buffer_limit: 0,
      cluster_not_found_response_code: 0,
      has_metadata_match: false,
      has_hash_policy: false,
      has_rate_limits: false,
      request_mirror_policies_count: 0,
      response_code: 0,
    };
    if !unsafe { callback(self.envoy_ptr, &mut result) } {
      return None;
    }
    let kind = RouteKind::from_abi(result.kind);
    let is_route_entry = kind == RouteKind::RouteEntry;
    Some(InputRoute {
      kind,
      name: unsafe { EnvoyBuffer::new_from_raw(result.name.ptr as *const u8, result.name.length) },
      virtual_host_name: unsafe {
        EnvoyBuffer::new_from_raw(
          result.virtual_host_name.ptr as *const u8,
          result.virtual_host_name.length,
        )
      },
      has_metadata: result.has_metadata,
      cluster_name: is_route_entry.then(|| unsafe {
        EnvoyBuffer::new_from_raw(
          result.cluster_name.ptr as *const u8,
          result.cluster_name.length,
        )
      }),
      timeout: is_route_entry.then(|| Duration::from_millis(result.timeout_ms)),
      idle_timeout: result
        .has_idle_timeout
        .then(|| Duration::from_millis(result.idle_timeout_ms)),
      max_stream_duration: result
        .has_max_stream_duration
        .then(|| Duration::from_millis(result.max_stream_duration_ms)),
      priority: is_route_entry.then_some(match result.priority {
        abi::envoy_dynamic_module_type_resource_priority::High => ResourcePriority::High,
        _ => ResourcePriority::Default,
      }),
      request_body_buffer_limit: is_route_entry.then_some(result.request_body_buffer_limit),
      cluster_not_found_response_code: is_route_entry
        .then_some(result.cluster_not_found_response_code),
      has_metadata_match: result.has_metadata_match,
      has_hash_policy: result.has_hash_policy,
      has_rate_limits: result.has_rate_limits,
      request_mirror_policies_count: result.request_mirror_policies_count,
      response_code: (!is_route_entry).then_some(result.response_code),
    })
  }

  /// Get the kind of the route that route matching resolved for the request.
  pub fn input_route_kind(&self) -> RouteKind {
    RouteKind::from_abi(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_kind(self.envoy_ptr)
    })
  }

  /// Get the name of the resolved route.
  pub fn input_route_name(&self) -> Option<EnvoyBuffer<'_>> {
    self.input_route_buffer(abi::envoy_dynamic_module_callback_route_specifier_get_input_route_name)
  }

  /// Get the name of the virtual host the resolved route belongs to.
  pub fn input_route_virtual_host_name(&self) -> Option<EnvoyBuffer<'_>> {
    self.input_route_buffer(
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_virtual_host_name,
    )
  }

  /// Get the upstream cluster of the resolved route.
  pub fn input_route_cluster_name(&self) -> Option<EnvoyBuffer<'_>> {
    self.input_route_buffer(
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_cluster_name,
    )
  }

  /// Get the location the resolved route redirects the request to.
  ///
  /// A non-empty result identifies a redirect, and a direct response that is not a redirect yields
  /// an empty result.
  pub fn input_route_redirect_location(&self) -> Option<EnvoyBuffer<'_>> {
    self.input_route_buffer(
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_redirect_location,
    )
  }

  /// Get the identifier recorded by [`RouteSpecifierContext::select_template`].
  pub fn selected_template_id(&self) -> Option<EnvoyBuffer<'_>> {
    self.input_route_buffer(
      abi::envoy_dynamic_module_callback_route_specifier_get_selected_template_id,
    )
  }

  /// Get the route timeout of the resolved route.
  pub fn input_route_timeout(&self) -> Option<Duration> {
    let mut timeout_ms: u64 = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_timeout(
        self.envoy_ptr,
        &mut timeout_ms,
      )
    } {
      Some(Duration::from_millis(timeout_ms))
    } else {
      None
    }
  }

  /// Get the status code the resolved route answers the request with.
  pub fn input_route_response_code(&self) -> Option<u32> {
    let mut status_code: u32 = 0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_response_code(
        self.envoy_ptr,
        &mut status_code,
      )
    } {
      Some(status_code)
    } else {
      None
    }
  }

  /// Get the string value of a route metadata entry of the resolved route.
  pub fn input_route_metadata_string(&self, namespace: &str, key: &str) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_metadata(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the string value of a metadata entry of the previous route of the stream.
  pub fn previous_route_metadata_string(
    &self,
    namespace: &str,
    key: &str,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_previous_route_metadata(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        &mut result,
      )
    } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }

  /// Get the number value of a route metadata entry of the resolved route.
  pub fn input_route_metadata_number(&self, namespace: &str, key: &str) -> Option<f64> {
    let mut result: f64 = 0.0;
    if unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route_metadata_number(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        &mut result,
      )
    } {
      Some(result)
    } else {
      None
    }
  }

  /// Select the route template Envoy generates the final route from, in place of the route the
  /// specifier was given, when the decision is left [`RouteDecision::Unspecified`].
  ///
  /// Returns `false` when the identifier is not declared in the route specifier configuration.
  /// A failed selection is handled by the failure policy when no template ends up selected and
  /// the decision is left [`RouteDecision::Unspecified`], rather than silently falling back to
  /// the route the specifier was given. A module that wants to probe for a template without
  /// committing checks the configuration getters instead.
  pub fn select_template(&mut self, template_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_template(
        self.envoy_ptr,
        crate::str_to_module_buffer(template_id),
      )
    }
  }

  /// Revert a [`RouteSpecifierContext::select_template`] call with the same identifier, so the
  /// route the specifier was given becomes the base of the final route again. The recorded
  /// overrides stay in effect.
  ///
  /// Returns `false` when the identifier does not name the selected template, in which case
  /// nothing changes.
  pub fn unselect_template(&mut self, template_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_unset_route_template(
        self.envoy_ptr,
        crate::str_to_module_buffer(template_id),
      )
    }
  }

  /// Record a u64 on the route this decision produces.
  ///
  /// It forces the produced route to be wrapped even with no other override, so that
  /// [`RouteSpecifierConfig::on_route_destroy`] fires for it with this value when the route is
  /// destroyed. It is effective when the decision is left [`RouteDecision::Unspecified`].
  pub fn set_route_user_data(&mut self, user_data: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_user_data(
        self.envoy_ptr,
        user_data,
      )
    }
  }

  /// Record a prefix rewrite of the request path sent upstream.
  ///
  /// `matched` must be a case insensitive prefix of the current path without its query string,
  /// which is replaced by `replacement` while the query string is preserved. Returns `false` when
  /// `matched` is not such a prefix or the rewritten path exceeds the configured maximum.
  pub fn set_prefix_rewrite(&mut self, matched: &str, replacement: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite(
        self.envoy_ptr,
        crate::str_to_module_buffer(matched),
        crate::str_to_module_buffer(replacement),
      )
    }
  }

  /// Record the decision that tells Envoy how to build the route.
  ///
  /// [`RouteDecision::Unspecified`] is in effect when the module records none, under which Envoy
  /// generates a new route based on what the setter methods of [`RouteSpecifierContext`]
  /// recorded. A later call replaces the decision of an earlier one.
  pub fn set_decision(&mut self, decision: RouteDecision) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_decision(
        self.envoy_ptr,
        decision.to_abi(),
      )
    }
  }

  /// Record the upstream cluster the request should use.
  ///
  /// Returns `false` when the name is empty or not a valid header value.
  pub fn set_cluster_name(&mut self, cluster_name: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_cluster_name(
        self.envoy_ptr,
        crate::str_to_module_buffer(cluster_name),
      )
    }
  }

  /// Record the route timeout for the request. A zero duration disables the timeout.
  pub fn set_timeout(&mut self, timeout: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_timeout(
        self.envoy_ptr,
        duration_to_millis(timeout),
      )
    }
  }

  /// Record the stream idle timeout for the request. A zero duration disables the idle timeout.
  pub fn set_idle_timeout(&mut self, timeout: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_idle_timeout(
        self.envoy_ptr,
        duration_to_millis(timeout),
      )
    }
  }

  /// Record the maximum stream duration for the request. A zero duration disables it.
  pub fn set_max_stream_duration(&mut self, duration: Duration) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_max_stream_duration(
        self.envoy_ptr,
        duration_to_millis(duration),
      )
    }
  }

  /// Record the request body buffer limit for the request.
  pub fn set_request_body_buffer_limit(&mut self, limit_bytes: u64) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_request_body_buffer_limit(
        self.envoy_ptr,
        limit_bytes,
      )
    }
  }

  /// Record the upstream resource priority for the request.
  pub fn set_priority(&mut self, priority: ResourcePriority) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_priority(
        self.envoy_ptr,
        priority.to_abi(),
      )
    }
  }

  /// Record the status code Envoy replies with when the selected cluster does not exist.
  ///
  /// Returns `false` when the status code is outside the range `[200, 600)`.
  pub fn set_cluster_not_found_response_code(&mut self, status_code: u32) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_cluster_not_found_response_code(
        self.envoy_ptr,
        status_code,
      )
    }
  }

  /// Select a route override declared in the route specifier configuration by override_id.
  ///
  /// Returns `false` when the override_id is not declared.
  pub fn set_route_override(&mut self, override_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_override(
        self.envoy_ptr,
        crate::str_to_module_buffer(override_id),
      )
    }
  }

  /// Revert a [`RouteSpecifierContext::set_route_override`] call with the same identifier, so
  /// nothing of the override applies to the final route. The values the module recorded itself
  /// stay in effect.
  ///
  /// Returns `false` when the identifier does not name the selected override, in which case
  /// nothing changes.
  pub fn unset_route_override(&mut self, override_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_unset_route_override(
        self.envoy_ptr,
        crate::str_to_module_buffer(override_id),
      )
    }
  }

  /// Record the string value of a route metadata entry.
  pub fn set_route_metadata_string(&mut self, namespace: &str, key: &str, value: &str) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
      )
    }
  }

  /// Record the number value of a route metadata entry.
  pub fn set_route_metadata_number(&mut self, namespace: &str, key: &str, value: f64) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_metadata_number(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        value,
      )
    }
  }

  /// Record the boolean value of a route metadata entry.
  pub fn set_route_metadata_bool(&mut self, namespace: &str, key: &str, value: bool) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_metadata_bool(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::str_to_module_buffer(key),
        value,
      )
    }
  }

  /// Record the typed route metadata of a namespace from a serialized `google.protobuf.Any`.
  ///
  /// Returns `false` when the bytes do not parse.
  pub fn set_route_typed_metadata(&mut self, namespace: &str, serialized_any: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_typed_metadata(
        self.envoy_ptr,
        crate::str_to_module_buffer(namespace),
        crate::bytes_to_module_buffer(serialized_any),
      )
    }
  }

  /// Record whether an HTTP filter is disabled for the request.
  ///
  /// Returns `false` when the name is empty.
  pub fn set_filter_disabled(&mut self, filter_name: &str, disabled: bool) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_filter_disabled(
        self.envoy_ptr,
        crate::str_to_module_buffer(filter_name),
        disabled,
      )
    }
  }

  /// Record the path, including any query string, of the request sent upstream.
  ///
  /// Returns `false` when the path does not start with `/` or is not a valid header value.
  pub fn set_path(&mut self, path: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_path(
        self.envoy_ptr,
        crate::str_to_module_buffer(path),
      )
    }
  }

  /// Record the authority of the request sent upstream.
  ///
  /// Returns `false` when the authority is not valid.
  pub fn set_host(&mut self, host: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_host(
        self.envoy_ptr,
        crate::str_to_module_buffer(host),
      )
    }
  }

  /// Record the name of the route the decision produces. The name is what the `%ROUTE_NAME%` access
  /// log command operator reports, so a module built route carries an identity of its own in access
  /// logs and other route name consumers.
  ///
  /// Returns `false` when the name is empty.
  pub fn set_route_name(&mut self, route_name: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_route_name(
        self.envoy_ptr,
        crate::str_to_module_buffer(route_name),
      )
    }
  }

  /// Record a header added to the request sent upstream.
  ///
  /// Returns `false` when the key is not a valid header name, is a pseudo header, or the value is
  /// not a valid header value.
  pub fn add_request_header(&mut self, key: &str, value: &str, action: HeaderAppendAction) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_add_request_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
        action.to_abi(),
      )
    }
  }

  /// Record a header removed from the request sent upstream.
  ///
  /// Returns `false` when the key is not a valid header name or is a pseudo header.
  pub fn remove_request_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_remove_request_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
      )
    }
  }

  /// Record a header added to the response.
  ///
  /// Returns `false` when the key is not a valid header name, is a pseudo header, or the value is
  /// not a valid header value.
  pub fn add_response_header(
    &mut self,
    key: &str,
    value: &str,
    action: HeaderAppendAction,
  ) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_add_response_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
        crate::str_to_module_buffer(value),
        action.to_abi(),
      )
    }
  }

  /// Record a header removed from the response.
  ///
  /// Returns `false` when the key is not a valid header name or is a pseudo header.
  pub fn remove_response_header(&mut self, key: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_remove_response_header(
        self.envoy_ptr,
        crate::str_to_module_buffer(key),
      )
    }
  }

  // The getters that write an Envoy owned buffer share this shape.
  fn input_route_buffer(
    &self,
    callback: unsafe extern "C" fn(
      *mut c_void,
      *mut abi::envoy_dynamic_module_type_envoy_buffer,
    ) -> bool,
  ) -> Option<EnvoyBuffer<'_>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    if unsafe { callback(self.envoy_ptr, &mut result) } {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) })
    } else {
      None
    }
  }
}

/// Trait that the dynamic module implements to decide the route of a request.
///
/// The configuration is created once at configuration time on the main thread and is shared by
/// every request the route specifier resolves a route for, so it must be `Send + Sync` and
/// read-only during resolution.
pub trait RouteSpecifierConfig: Send + Sync {
  /// Decide the route of a request.
  ///
  /// This is called while the route is being resolved, and again whenever the route is recomputed,
  /// so it must be able to reach a decision from the request and the stream info alone. The call
  /// is synchronous and cannot be time boxed, so it must not block or perform I/O.
  ///
  /// The decision that tells Envoy how to build the route is recorded with
  /// [`RouteSpecifierContext::set_decision`], with [`RouteDecision::Unspecified`] in effect when
  /// none is recorded. The returned status tells Envoy whether the route specifiers configured
  /// after this one run and whether route matching accepts the produced route.
  fn on_route(&self, ctx: &mut RouteSpecifierContext) -> OnRouteStatus;

  /// Called when a route built with [`RouteSpecifierContext::set_route_user_data`] is destroyed,
  /// with the value recorded on it. It may run on any thread, concurrently with other hooks, and
  /// possibly after the stream that installed the route is gone. It must not block or call back
  /// into Envoy. The default is a no op.
  fn on_route_destroy(&self, user_data: u64) {
    let _ = user_data;
  }
}

/// Envoy-side interface for the route specifier dynamic module.
///
/// It reports what the route specifier configuration declares, which a module uses to reject a
/// configuration that references a template or an override that Envoy does not know, and it
/// defines and records custom metrics scoped to that configuration. The declarations can be read at
/// any point, while metrics must be defined during configuration and can be recorded at any point.
///
/// Implementations must be `Send + Sync` since they may be accessed from multiple threads. The
/// handle borrows the Envoy side configuration, so a module must drop it together with the
/// [`RouteSpecifierConfig`] it was created for and must not keep it alive elsewhere.
#[cfg_attr(any(test, feature = "mock"), automock)]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyRouteSpecifierConfig: Send + Sync {
  /// The identifiers of the declared route templates, in configuration order.
  fn template_ids(&self) -> Vec<String>;

  /// The kind of the route a declared template produces, or [`RouteKind::None`] when the
  /// identifier is not declared.
  fn template_kind(&self, template_id: &str) -> RouteKind;

  /// Whether a route override with the given override_id is declared.
  fn has_route_override(&self, override_id: &str) -> bool;

  /// Register a route template from a serialized `envoy.config.route.v3.Route`.
  ///
  /// Envoy builds and validates it while the configuration is created, and the module selects it
  /// later with [`RouteSpecifierContext::select_template`]. Returns `false` when called outside
  /// configuration creation, when the route specifier is configured on a route configuration with no
  /// virtual host to build routes in, when the identifier is empty or already used, when the bytes do
  /// not parse, or when the route is invalid.
  fn register_route_template(&self, template_id: &str, serialized_route: &[u8]) -> bool;

  /// The specifier_instance_id of this configuration, empty when it is unset.
  fn specifier_instance_id(&self) -> String;

  /// Define a new counter with the given name and no labels.
  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new counter vec with the given name and label names.
  fn define_counter_vec<'a>(
    &self,
    name: &str,
    label_names: &[&'a str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge with the given name and no labels.
  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new gauge vec with the given name and label names.
  fn define_gauge_vec<'a>(
    &self,
    name: &str,
    label_names: &[&'a str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram with the given name and no labels.
  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Define a new histogram vec with the given name and label names.
  fn define_histogram_vec<'a>(
    &self,
    name: &str,
    label_names: &[&'a str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result>;

  /// Increment a counter by the given value.
  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increment a counter vec by the given value for the given label values.
  fn increment_counter_vec<'a>(
    &self,
    id: EnvoyCounterVecId,
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set a gauge to the given value.
  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Set a gauge vec to the given value for the given label values.
  fn set_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a gauge by the given value.
  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Increase a gauge vec by the given value for the given label values.
  fn increase_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a gauge by the given value.
  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Decrease a gauge vec by the given value for the given label values.
  fn decrease_gauge_vec<'a>(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value for a histogram.
  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;

  /// Record a value for a histogram vec for the given label values.
  fn record_histogram_value_vec<'a>(
    &self,
    id: EnvoyHistogramVecId,
    label_values: &[&'a str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result>;
}

fn metrics_result(
  res: abi::envoy_dynamic_module_type_metrics_result,
) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
  if res == abi::envoy_dynamic_module_type_metrics_result::Success {
    Ok(())
  } else {
    Err(res)
  }
}

/// Implementation of [`EnvoyRouteSpecifierConfig`] that calls into the Envoy ABI.
pub struct EnvoyRouteSpecifierConfigImpl {
  raw: abi::envoy_dynamic_module_type_route_specifier_config_envoy_ptr,
}

unsafe impl Send for EnvoyRouteSpecifierConfigImpl {}
unsafe impl Sync for EnvoyRouteSpecifierConfigImpl {}

impl EnvoyRouteSpecifierConfig for EnvoyRouteSpecifierConfigImpl {
  fn template_ids(&self) -> Vec<String> {
    let count = unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_get_template_count(self.raw)
    };
    let mut ids = Vec::with_capacity(count);
    for index in 0..count {
      let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: ptr::null_mut(),
        length: 0,
      };
      if unsafe {
        abi::envoy_dynamic_module_callback_route_specifier_config_get_template_id(
          self.raw,
          index,
          &mut result,
        )
      } {
        let id = unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length) };
        ids.push(String::from_utf8_lossy(id.as_slice()).into_owned());
      }
    }
    ids
  }

  fn template_kind(&self, template_id: &str) -> RouteKind {
    RouteKind::from_abi(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_get_template_kind(
        self.raw,
        crate::str_to_module_buffer(template_id),
      )
    })
  }

  fn has_route_override(&self, override_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_has_route_override(
        self.raw,
        crate::str_to_module_buffer(override_id),
      )
    }
  }

  fn register_route_template(&self, template_id: &str, serialized_route: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_register_route_template(
        self.raw,
        crate::str_to_module_buffer(template_id),
        crate::bytes_to_module_buffer(serialized_route),
      )
    }
  }

  fn specifier_instance_id(&self) -> String {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: ptr::null_mut(),
      length: 0,
    };
    let buffer = unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_get_specifier_instance_id(
        self.raw,
        &mut result,
      );
      EnvoyBuffer::new_from_raw(result.ptr as *const u8, result.length)
    };
    String::from_utf8_lossy(buffer.as_slice()).into_owned()
  }

  fn define_counter(
    &self,
    name: &str,
  ) -> Result<EnvoyCounterId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_counter(
        self.raw,
        crate::str_to_module_buffer(name),
        ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyCounterId(id))
  }

  fn define_counter_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyCounterVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_names = crate::strs_to_module_buffers(label_names);
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_counter(
        self.raw,
        crate::str_to_module_buffer(name),
        label_names.as_mut_ptr(),
        label_names.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyCounterVecId(id))
  }

  fn define_gauge(
    &self,
    name: &str,
  ) -> Result<EnvoyGaugeId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_gauge(
        self.raw,
        crate::str_to_module_buffer(name),
        ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeId(id))
  }

  fn define_gauge_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyGaugeVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_names = crate::strs_to_module_buffers(label_names);
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_gauge(
        self.raw,
        crate::str_to_module_buffer(name),
        label_names.as_mut_ptr(),
        label_names.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyGaugeVecId(id))
  }

  fn define_histogram(
    &self,
    name: &str,
  ) -> Result<EnvoyHistogramId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_histogram(
        self.raw,
        crate::str_to_module_buffer(name),
        ptr::null_mut(),
        0,
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramId(id))
  }

  fn define_histogram_vec(
    &self,
    name: &str,
    label_names: &[&str],
  ) -> Result<EnvoyHistogramVecId, abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_names = crate::strs_to_module_buffers(label_names);
    let mut id: usize = 0;
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_define_histogram(
        self.raw,
        crate::str_to_module_buffer(name),
        label_names.as_mut_ptr(),
        label_names.len(),
        &mut id,
      )
    })?;
    Ok(EnvoyHistogramVecId(id))
  }

  fn increment_counter(
    &self,
    id: EnvoyCounterId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_increment_counter(
        self.raw,
        id.0,
        ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increment_counter_vec(
    &self,
    id: EnvoyCounterVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = crate::strs_to_module_buffers(label_values);
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_increment_counter(
        self.raw,
        id.0,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }

  fn set_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_set_gauge(
        self.raw,
        id.0,
        ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn set_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = crate::strs_to_module_buffers(label_values);
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_set_gauge(
        self.raw,
        id.0,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }

  fn increase_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_increment_gauge(
        self.raw,
        id.0,
        ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn increase_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = crate::strs_to_module_buffers(label_values);
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_increment_gauge(
        self.raw,
        id.0,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }

  fn decrease_gauge(
    &self,
    id: EnvoyGaugeId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_decrement_gauge(
        self.raw,
        id.0,
        ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn decrease_gauge_vec(
    &self,
    id: EnvoyGaugeVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = crate::strs_to_module_buffers(label_values);
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_decrement_gauge(
        self.raw,
        id.0,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }

  fn record_histogram_value(
    &self,
    id: EnvoyHistogramId,
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_record_histogram_value(
        self.raw,
        id.0,
        ptr::null_mut(),
        0,
        value,
      )
    })
  }

  fn record_histogram_value_vec(
    &self,
    id: EnvoyHistogramVecId,
    label_values: &[&str],
    value: u64,
  ) -> Result<(), abi::envoy_dynamic_module_type_metrics_result> {
    let mut label_values = crate::strs_to_module_buffers(label_values);
    metrics_result(unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_config_record_histogram_value(
        self.raw,
        id.0,
        label_values.as_mut_ptr(),
        label_values.len(),
        value,
      )
    })
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_route_specifier_config_new(
    config_envoy_ptr: abi::envoy_dynamic_module_type_route_specifier_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
  ) -> *const c_void {
    // SAFETY: `name` is a protobuf string (UTF-8 by contract) and `config` is opaque bytes.
    // The helpers tolerate `(nullptr, 0)` empty inputs and substitute `U+FFFD` for malformed
    // UTF-8 rather than triggering UB.
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_bytes = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };

    envoy_dynamic_module_on_route_specifier_config_new_impl(
      config_envoy_ptr,
      name_str.as_ref(),
      config_bytes,
      crate::NEW_ROUTE_SPECIFIER_CONFIG_FUNCTION
        .get()
        .expect("NEW_ROUTE_SPECIFIER_CONFIG_FUNCTION must be set"),
    )
  }
  on_panic = ptr::null()
}

/// Testable wrapper for [`envoy_dynamic_module_on_route_specifier_config_new`].
///
/// The FFI entry point extracts the inputs and resolves the registered factory. This function
/// performs the `Option`-to-pointer conversion that unit tests can drive directly.
pub fn envoy_dynamic_module_on_route_specifier_config_new_impl(
  config_envoy_ptr: abi::envoy_dynamic_module_type_route_specifier_config_envoy_ptr,
  name: &str,
  config: &[u8],
  new_fn: &crate::NewRouteSpecifierConfigFunction,
) -> *const c_void {
  let envoy_config: Arc<dyn EnvoyRouteSpecifierConfig> = Arc::new(EnvoyRouteSpecifierConfigImpl {
    raw: config_envoy_ptr,
  });
  match new_fn(name, config, envoy_config) {
    Some(config) => crate::wrap_into_c_void_ptr!(config),
    None => ptr::null(),
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_route_specifier_config_destroy(
    config_ptr: *const c_void,
  ) {
    crate::drop_wrapped_c_void_ptr!(config_ptr, RouteSpecifierConfig);
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_route_specifier_on_route(
    config_ptr: abi::envoy_dynamic_module_type_route_specifier_config_module_ptr,
    context_envoy_ptr: abi::envoy_dynamic_module_type_route_specifier_context_envoy_ptr,
  ) -> abi::envoy_dynamic_module_type_route_specifier_on_route_status {
    let config = &*(config_ptr as *const Box<dyn RouteSpecifierConfig>);
    let mut ctx = unsafe { RouteSpecifierContext::new(context_envoy_ptr) };
    config.on_route(&mut ctx).to_abi()
  }
  // A panic during resolution must not look like a decision, so record the Error decision, which
  // replaces whatever the module recorded before panicking, and let Envoy apply the configured
  // failure policy. The returned status is then not acted on.
  on_panic = {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_decision(
        context_envoy_ptr,
        abi::envoy_dynamic_module_type_route_specifier_decision::Error,
      );
    }
    abi::envoy_dynamic_module_type_route_specifier_on_route_status::Continue
  }
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_route_specifier_route_destroy(
    config_ptr: abi::envoy_dynamic_module_type_route_specifier_config_module_ptr,
    user_data: u64,
  ) {
    let config = &*(config_ptr as *const Box<dyn RouteSpecifierConfig>);
    config.on_route_destroy(user_data);
  }
}
