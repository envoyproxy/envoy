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
use mockall::*;
use std::ffi::c_void;
use std::ptr;
use std::sync::Arc;
use std::time::Duration;

/// The decision a route specifier returns for a request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteDecision {
  /// Use the route the specifier was given, unchanged. Recorded overrides are ignored.
  PassThrough,
  /// Use the route the specifier was given with the recorded overrides applied.
  Override,
  /// Use the template recorded with [`RouteSpecifierContext::select_template`], evaluated against
  /// the request like a configured route, with the recorded overrides applied.
  SelectTemplate,
  /// Use no route, so the request is handled as if nothing had matched.
  NoRoute,
  /// The module could not reach a decision, so Envoy applies the configured failure policy.
  Error,
}

impl RouteDecision {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_specifier_decision {
    match self {
      Self::PassThrough => abi::envoy_dynamic_module_type_route_specifier_decision::PassThrough,
      Self::Override => abi::envoy_dynamic_module_type_route_specifier_decision::Override,
      Self::SelectTemplate => {
        abi::envoy_dynamic_module_type_route_specifier_decision::SelectTemplate
      },
      Self::NoRoute => abi::envoy_dynamic_module_type_route_specifier_decision::NoRoute,
      Self::Error => abi::envoy_dynamic_module_type_route_specifier_decision::Error,
    }
  }

  fn from_abi(value: abi::envoy_dynamic_module_type_route_specifier_decision) -> Self {
    match value {
      abi::envoy_dynamic_module_type_route_specifier_decision::PassThrough => Self::PassThrough,
      abi::envoy_dynamic_module_type_route_specifier_decision::Override => Self::Override,
      abi::envoy_dynamic_module_type_route_specifier_decision::SelectTemplate => {
        Self::SelectTemplate
      },
      abi::envoy_dynamic_module_type_route_specifier_decision::NoRoute => Self::NoRoute,
      _ => Self::Error,
    }
  }
}

/// Whether the route specifiers configured after this one run for the request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChainStatus {
  /// The chain continues.
  Continue,
  /// The chain stops and the result of this specifier is the final route.
  StopIteration,
}

impl ChainStatus {
  fn to_abi(self) -> abi::envoy_dynamic_module_type_route_specifier_chain_status {
    match self {
      Self::Continue => abi::envoy_dynamic_module_type_route_specifier_chain_status::Continue,
      Self::StopIteration => {
        abi::envoy_dynamic_module_type_route_specifier_chain_status::StopIteration
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

/// Why Envoy could not honor the decision of a module.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteFailure {
  /// The decision was honored.
  None,
  /// The module returned [`RouteDecision::Error`].
  ModuleError,
  /// The decision was [`RouteDecision::SelectTemplate`] without a successful template selection.
  TemplateNotSelected,
  /// The match of the selected template does not hold for the request.
  TemplateMatchFailed,
  /// The decision was [`RouteDecision::Override`] while route matching resolved no route.
  OverrideWithoutRoute,
  /// Route entry overrides were recorded for a route that answers the request directly.
  OverrideOnNonRouteEntry,
  /// The recorded route metadata was rejected by a typed metadata factory.
  RouteMetadata,
}

impl RouteFailure {
  fn from_abi(value: abi::envoy_dynamic_module_type_route_specifier_failure) -> Self {
    match value {
      abi::envoy_dynamic_module_type_route_specifier_failure::None => Self::None,
      abi::envoy_dynamic_module_type_route_specifier_failure::ModuleError => Self::ModuleError,
      abi::envoy_dynamic_module_type_route_specifier_failure::TemplateNotSelected => {
        Self::TemplateNotSelected
      },
      abi::envoy_dynamic_module_type_route_specifier_failure::TemplateMatchFailed => {
        Self::TemplateMatchFailed
      },
      abi::envoy_dynamic_module_type_route_specifier_failure::OverrideWithoutRoute => {
        Self::OverrideWithoutRoute
      },
      abi::envoy_dynamic_module_type_route_specifier_failure::OverrideOnNonRouteEntry => {
        Self::OverrideOnNonRouteEntry
      },
      abi::envoy_dynamic_module_type_route_specifier_failure::RouteMetadata => Self::RouteMetadata,
    }
  }
}

/// One property that shadow mode compares.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum CompareField {
  RouteKind = 1,
  ClusterName = 2,
  Timeout = 3,
  IdleTimeout = 4,
  MaxStreamDuration = 5,
  Priority = 6,
  RequestBodyBufferLimit = 7,
  ClusterNotFoundResponseCode = 8,
  RetryPolicy = 9,
  HedgePolicy = 10,
  MetadataMatch = 11,
  HashPolicy = 12,
  RequestMirrorPolicies = 13,
  RequestPath = 14,
  RequestAuthority = 15,
  RequestHeaders = 16,
  ResponseHeaders = 17,
  FilterDisabled = 18,
  ResponseCode = 19,
  RedirectLocation = 20,
  VirtualHostName = 21,
  RouteMetadata = 22,
  DirectResponseBody = 23,
  RouteName = 24,
  RateLimitPolicy = 25,
  Cors = 26,
  Tracing = 27,
}

/// The properties that differed between the route of the module and the route of the route table
/// it is being validated against.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ShadowMismatches(pub u64);

impl ShadowMismatches {
  /// Whether the two routes were equivalent.
  pub fn is_match(&self) -> bool {
    self.0 == 0
  }

  /// Whether the given property differed.
  pub fn contains(&self, field: CompareField) -> bool {
    self.0 & (1u64 << (field as u32)) != 0
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

/// Context for a single route decision.
///
/// It provides read access to the request, to the stream info and to the route that route matching
/// resolved, and the setters that record the decision. A context is valid only for the duration of
/// a single [`RouteSpecifierConfig::on_route`] or [`RouteSpecifierConfig::on_shadow_result`] call
/// and must not be stored. The setters do nothing during the latter, where the decision has
/// already been made.
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

  /// Get all request headers as key-value [`EnvoyBuffer`] pairs.
  ///
  /// Returns an empty vector when there are no headers.
  pub fn get_all_request_headers(&self) -> Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> {
    let count = self.get_request_headers_count();
    if count == 0 {
      return Vec::new();
    }
    // Fill the pairs in place as ABI headers to avoid a second allocation.
    let mut headers: Vec<(EnvoyBuffer<'_>, EnvoyBuffer<'_>)> = Vec::with_capacity(count);
    let success = unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_request_headers(
        self.envoy_ptr,
        headers.as_mut_ptr() as *mut abi::envoy_dynamic_module_type_envoy_http_header,
      )
    };
    if !success {
      return Vec::new();
    }
    unsafe {
      headers.set_len(count);
    }
    headers
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
    if !unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_get_input_route(
        self.envoy_ptr,
        &mut result,
      )
    } {
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

  /// Select the route template the [`RouteDecision::SelectTemplate`] decision uses.
  ///
  /// Returns `false` when the identifier is not declared in the route specifier configuration.
  pub fn select_template(&mut self, template_id: &str) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_template(
        self.envoy_ptr,
        crate::str_to_module_buffer(template_id),
      )
    }
  }

  /// Select whether the route specifiers configured after this one run for the request.
  pub fn set_chain_status(&mut self, status: ChainStatus) {
    unsafe {
      abi::envoy_dynamic_module_callback_route_specifier_set_chain_status(
        self.envoy_ptr,
        status.to_abi(),
      )
    }
  }

  /// Record the upstream cluster the request should use.
  ///
  /// Returns `false` when the name is not a valid header value or is not allowed by the route
  /// specifier configuration.
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

  /// Record the string value of a route metadata entry.
  ///
  /// Returns `false` when the namespace is not allowed by the route specifier configuration.
  pub fn set_route_metadata_string(&mut self, namespace: &str, key: &str, value: &str) -> bool {
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
  ///
  /// Returns `false` when the namespace is not allowed by the route specifier configuration.
  pub fn set_route_metadata_number(&mut self, namespace: &str, key: &str, value: f64) -> bool {
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
  ///
  /// Returns `false` when the namespace is not allowed by the route specifier configuration.
  pub fn set_route_metadata_bool(&mut self, namespace: &str, key: &str, value: bool) -> bool {
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
  /// Returns `false` when the namespace is not allowed or the bytes do not parse.
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
  fn on_route(&self, ctx: &mut RouteSpecifierContext) -> RouteDecision;

  /// Report the outcome of comparing the route of the module with the route that route matching
  /// resolved, in shadow mode. The default implementation does nothing.
  fn on_shadow_result(
    &self,
    _ctx: &RouteSpecifierContext,
    _decision: RouteDecision,
    _failure: RouteFailure,
    _mismatches: ShadowMismatches,
  ) {
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
#[automock]
#[allow(clippy::needless_lifetimes)]
pub trait EnvoyRouteSpecifierConfig: Send + Sync {
  /// The identifiers of the declared route templates, in configuration order.
  fn template_ids(&self) -> Vec<String>;

  /// The kind of the route a declared template produces, or [`RouteKind::None`] when the
  /// identifier is not declared.
  fn template_kind(&self, template_id: &str) -> RouteKind;

  /// Whether a route override with the given override_id is declared.
  fn has_route_override(&self, override_id: &str) -> bool;

  /// Whether the route specifier runs in shadow mode.
  fn is_shadow_mode(&self) -> bool;

  /// Register a route template from a serialized `envoy.config.route.v3.Route`.
  ///
  /// Envoy builds and validates it while the configuration is created, and the module selects it
  /// later with [`RouteSpecifierContext::select_template`]. Returns `false` when called outside
  /// configuration creation, when the route specifier is configured on a route configuration with no
  /// virtual host to build routes in, when the identifier is empty or already used, when the bytes do
  /// not parse, or when the route is invalid.
  fn register_route_template(&self, template_id: &str, serialized_route: &[u8]) -> bool;

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

  fn is_shadow_mode(&self) -> bool {
    unsafe { abi::envoy_dynamic_module_callback_route_specifier_config_is_shadow_mode(self.raw) }
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
  ) -> abi::envoy_dynamic_module_type_route_specifier_decision {
    let config = &*(config_ptr as *const Box<dyn RouteSpecifierConfig>);
    let mut ctx = unsafe { RouteSpecifierContext::new(context_envoy_ptr) };
    config.on_route(&mut ctx).to_abi()
  }
  // A panic during resolution must not look like a decision, so fail closed and let Envoy apply
  // the configured failure policy.
  on_panic = abi::envoy_dynamic_module_type_route_specifier_decision::Error
}

ffi_export! {
  /// # Safety
  ///
  /// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
  /// by the Envoy dynamic module ABI.
  unsafe fn envoy_dynamic_module_on_route_specifier_shadow_result(
    config_ptr: abi::envoy_dynamic_module_type_route_specifier_config_module_ptr,
    context_envoy_ptr: abi::envoy_dynamic_module_type_route_specifier_context_envoy_ptr,
    decision: abi::envoy_dynamic_module_type_route_specifier_decision,
    failure: abi::envoy_dynamic_module_type_route_specifier_failure,
    mismatch_mask: u64,
  ) {
    let config = &*(config_ptr as *const Box<dyn RouteSpecifierConfig>);
    let ctx = unsafe { RouteSpecifierContext::new(context_envoy_ptr) };
    config.on_shadow_result(
      &ctx,
      RouteDecision::from_abi(decision),
      RouteFailure::from_abi(failure),
      ShadowMismatches(mismatch_mask),
    );
  }
}
