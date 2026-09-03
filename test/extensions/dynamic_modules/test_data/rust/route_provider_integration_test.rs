//! Integration test module for route provider dynamic modules.
//!
//! This module registers a route provider through the `route_provider:` arm of
//! `declare_all_init_functions!`. The selection is driven entirely by request headers so the test
//! can control every branch, and it exercises the full route provider context ABI surface: request
//! headers (single value, indexed value, count, and bulk iteration), stream info attributes
//! (string, int, and bool), the random value, the scalar route action setters, the named route
//! action overrides, the named per-route configuration overrides, the route metadata setters, the
//! header mutations, the path rewrite, and the terminal direct response and redirect.
//!
//! The headers the module reads are:
//!   `x-no-route`      when present the module selects no route, so Envoy resolves no route.
//!   `x-decide-no-select`       when present the module reports a decision without selecting a
//!                              route, so Envoy resolves no route.
//!   `route-index`     the index of the route template to select. Defaults to 0. An out of range
//!                     index makes select_route fail, so the module selects no route.
//!   `x-cluster`       the cluster to route to, replacing the cluster of the route template.
//!   `x-echo`          the name of a read accessor whose value is set as the `x-echo-result`
//!                     response header, so that a test can assert on what the module read across
//!                     the ABI boundary.
//!   `x-timeout-ms`    the route timeout to set, in milliseconds.
//!   `x-idle-timeout-ms`        the stream idle timeout to set, in milliseconds.
//!   `x-max-stream-duration-ms` the maximum stream duration to set, in milliseconds.
//!   `x-buffer-limit`  the request body buffer limit to set, in bytes.
//!   `x-priority`      `high` or `default`, the upstream resource priority to set.
//!   `x-override`      the name of the route action override to select.
//!   `x-per-route-config`       the name of the per-route configuration override to select.
//!   `x-route-meta-string`      a string value set as route metadata under `envoy.test.route`.
//!   `x-route-meta-number`      a number value set as route metadata under `envoy.test.route`.
//!   `x-route-meta-bool`        a bool value set as route metadata under `envoy.test.route`.
//!   `x-route-meta-struct`      merges a fixed `google.protobuf.Struct` into the route metadata.
//!   `x-route-typed-meta`       sets typed route metadata. A value of `throw` picks one the
//!                              registered factory rejects.
//!   `x-route-meta-bad-struct`  sets route metadata from a malformed struct buffer, a no-op.
//!   `x-route-meta-bad-typed`   sets typed route metadata from a malformed any buffer, a no-op.
//!   `x-req-header-add`         appends the value as the `x-module-req` request header.
//!   `x-req-header-remove`      removes the `x-remove-me` request header.
//!   `x-resp-header-set`        overwrites the value as the `x-module-resp` response header.
//!   `x-set-path`               replaces the request path verbatim.
//!   `x-direct-response`        a status code the request resolves to a direct response with.
//!   `x-redirect`               a status code the request resolves to a redirect with.
//!
//! A scalar header holding `max` selects the largest value the SDK type can express, which exercises
//! the saturating conversion at the ABI boundary.

use envoy_proxy_dynamic_modules_rust_sdk::route_provider::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::time::Duration;

declare_all_init_functions!(init, route_provider: new_route_provider_config_fn);

fn init() -> bool {
  true
}

/// Route provider factory function: dispatches to the config implementation based on
/// `provider_name`. Returning `None` for an unknown name causes Envoy to reject the route provider
/// configuration at config load time.
fn new_route_provider_config_fn(
  name: &str,
  config: &[u8],
  route_template_count: usize,
) -> Option<Box<dyn RouteProviderConfig>> {
  match name {
    "test_route_provider" => Some(Box::new(TestRouteProviderConfig {
      cluster_prefix: String::from_utf8_lossy(config).into_owned(),
      route_template_count,
    })),
    _ => None,
  }
}

struct TestRouteProviderConfig {
  cluster_prefix: String,
  route_template_count: usize,
}

/// Stands in for a value the module could not read, so that the absent case is observable too.
const ABSENT: &str = "absent";

fn buffer_to_string(buffer: EnvoyBuffer) -> String {
  String::from_utf8_lossy(buffer.as_slice()).into_owned()
}

fn buffer_to_string_or_absent(buffer: Option<EnvoyBuffer>) -> String {
  buffer.map_or_else(|| ABSENT.to_owned(), buffer_to_string)
}

fn read_u64_header(ctx: &RouteProviderContext, key: &str) -> Option<u64> {
  let value = ctx.get_request_header(key)?;
  if value.as_slice() == b"max" {
    return Some(u64::MAX);
  }
  std::str::from_utf8(value.as_slice()).ok()?.parse().ok()
}

/// Reads the accessor named by the `x-echo` header. An unknown name reads nothing so that a test
/// can tell a missing value apart from a typo in the test itself.
fn read_echoed_value(
  ctx: &RouteProviderContext,
  name: &[u8],
  config: &TestRouteProviderConfig,
) -> String {
  use abi::envoy_dynamic_module_type_attribute_id as AttributeId;
  match name {
    b"header-count" => ctx.get_request_headers_count().to_string(),
    b"header-value" => buffer_to_string_or_absent(ctx.get_request_header("x-multi")),
    b"header-value-index" => buffer_to_string_or_absent(
      ctx
        .get_request_header_value("x-multi", 1)
        .map(|(value, _)| value),
    ),
    b"header-value-total" => ctx
      .get_request_header_value("x-multi", 0)
      .map_or_else(|| ABSENT.to_owned(), |(_, total)| total.to_string()),
    b"header-bulk" => ctx
      .get_all_request_headers()
      .into_iter()
      .find(|(key, _)| key.as_slice() == b"x-multi")
      .map_or_else(|| ABSENT.to_owned(), |(_, value)| buffer_to_string(value)),
    b"attribute-string" => {
      buffer_to_string_or_absent(ctx.get_attribute_string(AttributeId::RequestProtocol))
    },
    b"attribute-string-unset" => {
      buffer_to_string_or_absent(ctx.get_attribute_string(AttributeId::ResponseCodeDetails))
    },
    b"attribute-int" => ctx
      .get_attribute_int(AttributeId::UpstreamRequestAttemptCount)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"attribute-bool" => ctx
      .get_attribute_bool(AttributeId::HealthCheck)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"random-value" => ctx.random_value().to_string(),
    b"template-count" => config.route_template_count.to_string(),
    _ => ABSENT.to_owned(),
  }
}

impl RouteProviderConfig for TestRouteProviderConfig {
  fn on_select(&self, ctx: &mut RouteProviderContext) -> bool {
    if ctx.get_request_header("x-no-route").is_some() {
      return false;
    }
    // Reports a decision without selecting a route, so Envoy resolves no route.
    if ctx.get_request_header("x-decide-no-select").is_some() {
      return true;
    }
    let index = read_u64_header(ctx, "route-index").unwrap_or(0) as usize;
    // An out of range index leaves the module with no route to apply overrides to.
    if !ctx.select_route(index) {
      return false;
    }

    if let Some(buffer) = ctx.get_request_header("x-cluster") {
      ctx.set_cluster_name(&format!(
        "{}{}",
        self.cluster_prefix,
        buffer_to_string(buffer)
      ));
    }
    // Echo a read accessor value into a response header so a test can assert what the module read
    // across the ABI boundary.
    if let Some(echo) = ctx.get_request_header("x-echo") {
      let value = read_echoed_value(ctx, echo.as_slice(), self);
      ctx.set_response_header("x-echo-result", &value, HeaderMutation::Overwrite);
    }

    if let Some(name) = ctx.get_request_header("x-override") {
      // Tests also pass names that are not declared, so a miss is expected here.
      let _ = ctx.set_route_action_override(&buffer_to_string(name));
    }

    if let Some(name) = ctx.get_request_header("x-per-route-config") {
      // Tests also pass names that are not declared, so a miss is expected here.
      let _ = ctx.set_per_route_config_override(&buffer_to_string(name));
    }

    if let Some(timeout_ms) = read_u64_header(ctx, "x-timeout-ms") {
      ctx.set_timeout(Duration::from_millis(timeout_ms));
    }
    if let Some(idle_timeout_ms) = read_u64_header(ctx, "x-idle-timeout-ms") {
      ctx.set_idle_timeout(Duration::from_millis(idle_timeout_ms));
    }
    if let Some(max_stream_duration_ms) = read_u64_header(ctx, "x-max-stream-duration-ms") {
      ctx.set_max_stream_duration(Duration::from_millis(max_stream_duration_ms));
    }
    if let Some(limit) = read_u64_header(ctx, "x-buffer-limit") {
      ctx.set_request_body_buffer_limit(limit);
    }
    if let Some(priority) =
      ctx
        .get_request_header("x-priority")
        .map(|buffer| match buffer.as_slice() {
          b"high" => ResourcePriority::High,
          _ => ResourcePriority::Default,
        })
    {
      ctx.set_priority(priority);
    }

    set_route_metadata(ctx);

    if let Some(value) = ctx.get_request_header("x-req-header-add") {
      ctx.set_request_header(
        "x-module-req",
        &buffer_to_string(value),
        HeaderMutation::Append,
      );
    }
    if ctx.get_request_header("x-req-header-remove").is_some() {
      ctx.set_request_header("x-remove-me", "", HeaderMutation::Remove);
    }
    if let Some(value) = ctx.get_request_header("x-resp-header-set") {
      ctx.set_response_header(
        "x-module-resp",
        &buffer_to_string(value),
        HeaderMutation::Overwrite,
      );
    }
    if let Some(path) = ctx.get_request_header("x-set-path") {
      ctx.set_path(&buffer_to_string(path));
    }

    if let Some(code) = read_u64_header(ctx, "x-direct-response") {
      let _ = ctx.set_direct_response(
        u32::try_from(code).unwrap_or(u32::MAX),
        "direct-response-body",
      );
    }
    if let Some(code) = read_u64_header(ctx, "x-redirect") {
      let _ = ctx.set_redirect(
        u32::try_from(code).unwrap_or(u32::MAX),
        "http://redirect.example.com/new",
      );
    }
    true
  }
}

/// Route metadata setters, each guarded by its own header so a test can drive them in isolation.
fn set_route_metadata(ctx: &mut RouteProviderContext) {
  if let Some(value) = ctx.get_request_header("x-route-meta-string") {
    ctx.set_route_metadata_string("envoy.test.route", "string_key", &buffer_to_string(value));
  }
  if let Some(value) = ctx
    .get_request_header("x-route-meta-number")
    .and_then(|buffer| {
      std::str::from_utf8(buffer.as_slice())
        .ok()?
        .parse::<f64>()
        .ok()
    })
  {
    ctx.set_route_metadata_number("envoy.test.route", "number_key", value);
  }
  if let Some(value) = ctx.get_request_header("x-route-meta-bool") {
    ctx.set_route_metadata_bool("envoy.test.route", "bool_key", value.as_slice() == b"true");
  }
  if ctx.get_request_header("x-route-meta-struct").is_some() {
    // Hand-encoded google.protobuf.Struct because the test module has no protobuf dependency.
    //   0a 1a                      field 1 (fields) length 26
    //     0a 0a "struct_key"       entry key
    //     12 0c 1a 0a "struct-val" entry value, a string Value
    let serialized_struct: &[u8] = &[
      0x0a, 0x1a, 0x0a, 0x0a, 0x73, 0x74, 0x72, 0x75, 0x63, 0x74, 0x5f, 0x6b, 0x65, 0x79, 0x12,
      0x0c, 0x1a, 0x0a, 0x73, 0x74, 0x72, 0x75, 0x63, 0x74, 0x2d, 0x76, 0x61, 0x6c,
    ];
    ctx.set_route_metadata_struct("envoy.test.route", serialized_struct);
  }
  // Malformed buffers exercise the parse-failure paths, which leave the metadata unchanged.
  if ctx.get_request_header("x-route-meta-bad-struct").is_some() {
    ctx.set_route_metadata_struct("envoy.test.route", &[0xff, 0xff, 0xff]);
  }
  if ctx.get_request_header("x-route-meta-bad-typed").is_some() {
    ctx.set_route_typed_metadata("envoy.test.typed_route", &[0xff, 0xff, 0xff]);
  }
  if let Some(value) = ctx.get_request_header("x-route-typed-meta") {
    // Hand-encoded google.protobuf.Any because the test module has no protobuf dependency. A
    // `throw` value picks a type_url the registered factory rejects, any other value picks one it
    // accepts.
    let serialized_any: &[u8] = if value.as_slice() == b"throw" {
      //   0a 07 74 2f 74 68 72 6f 77   field 1 (type_url) = "t/throw"
      //   12 02 01 02                  field 2 (value)    = 0x01 0x02
      &[
        0x0a, 0x07, 0x74, 0x2f, 0x74, 0x68, 0x72, 0x6f, 0x77, 0x12, 0x02, 0x01, 0x02,
      ]
    } else {
      //   0a 03 74 2f 78   field 1 (type_url) = "t/x"
      //   12 02 01 02      field 2 (value)    = 0x01 0x02
      &[0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02]
    };
    ctx.set_route_typed_metadata("envoy.test.typed_route", serialized_any);
  }
}
