//! Integration test module for cluster specifier dynamic modules.
//!
//! This module registers a cluster specifier through the `cluster_specifier:` arm of
//! `declare_all_init_functions!`. The selection is driven entirely by request headers so the test
//! can control every branch, and it exercises the full cluster specifier context ABI surface:
//! request headers (single value, indexed value, count, and bulk iteration), stream info attributes
//! (string, int, and bool), dynamic metadata, the route name, the random value, the scalar route
//! action setters, and the named route action overrides.
//!
//! The headers the module reads are:
//!   `env`               the cluster to route to, prefixed with the `specifier_config` bytes.
//!                       Without it the module reports no decision so the properties of the matched
//!                       route stay in effect.
//!   `x-override`        the name of the route action override to select.
//!   `x-timeout-ms`      the route timeout to set, in milliseconds.
//!   `x-idle-timeout-ms` the stream idle timeout to set, in milliseconds.
//!   `x-buffer-limit`    the request body buffer limit to set, in bytes.
//!   `x-priority`        `high` or `default`, the upstream resource priority to set.
//!   `x-echo`            the name of a read accessor whose value replaces the cluster name, so that
//!                       a test can assert on what the module read across the ABI boundary.
//!   `x-retry-cluster`   the cluster to route to once the first upstream attempt has been made, so
//!                       that a test can tell that a retry re-entered the module.
//!
//! A scalar header holding `max` selects the largest value the SDK type can express, which exercises
//! the saturating conversion at the ABI boundary.

use envoy_proxy_dynamic_modules_rust_sdk::cluster_specifier::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::time::Duration;

declare_all_init_functions!(init, cluster_specifier: new_cluster_specifier_config_fn);

fn init() -> bool {
  true
}

/// Cluster specifier factory function: dispatches to the config implementation based on
/// `specifier_name`. Returning `None` for an unknown name causes Envoy to reject the cluster
/// specifier configuration at config load time.
fn new_cluster_specifier_config_fn(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn ClusterSpecifierConfig>> {
  match name {
    "test_cluster_specifier" => Some(Box::new(TestClusterSpecifierConfig {
      cluster_prefix: String::from_utf8_lossy(config).into_owned(),
    })),
    _ => None,
  }
}

struct TestClusterSpecifierConfig {
  cluster_prefix: String,
}

/// Stands in for a value the module could not read, so that the absent case is observable too.
const ABSENT: &str = "absent";

fn buffer_to_string(buffer: EnvoyBuffer) -> String {
  String::from_utf8_lossy(buffer.as_slice()).into_owned()
}

fn buffer_to_string_or_absent(buffer: Option<EnvoyBuffer>) -> String {
  buffer.map_or_else(|| ABSENT.to_owned(), buffer_to_string)
}

fn read_u64_header(ctx: &ClusterSpecifierContext, key: &str) -> Option<u64> {
  let value = ctx.get_request_header(key)?;
  if value.as_slice() == b"max" {
    return Some(u64::MAX);
  }
  std::str::from_utf8(value.as_slice()).ok()?.parse().ok()
}

/// Reads the accessor named by the `x-echo` header. An unknown name reads nothing so that a test
/// can tell a missing value apart from a typo in the test itself.
fn read_echoed_value(ctx: &ClusterSpecifierContext, name: &[u8]) -> String {
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
    b"dynamic-metadata" => {
      buffer_to_string_or_absent(ctx.get_dynamic_metadata("envoy.test", "shard"))
    },
    b"route-name" => buffer_to_string_or_absent(ctx.route_name()),
    b"random-value" => ctx.random_value().to_string(),
    _ => ABSENT.to_owned(),
  }
}

impl ClusterSpecifierConfig for TestClusterSpecifierConfig {
  fn on_select(&self, ctx: &mut ClusterSpecifierContext) -> bool {
    // A request without the header leaves the properties of the matched route in effect.
    let env = match ctx.get_request_header("env") {
      Some(buffer) => buffer_to_string(buffer),
      None => return false,
    };
    // A retry re-enters the module, so naming a different cluster for the attempts after the first
    // lets a test tell that the re-selection took effect.
    let retry_cluster = ctx
      .get_request_header("x-retry-cluster")
      .map(buffer_to_string);
    let is_retry = ctx
      .get_attribute_int(abi::envoy_dynamic_module_type_attribute_id::UpstreamRequestAttemptCount)
      .is_some_and(|attempt_count| attempt_count > 1);

    let cluster_name = match retry_cluster {
      Some(cluster) if is_retry => cluster,
      _ => match ctx.get_request_header("x-echo") {
        Some(buffer) => read_echoed_value(ctx, buffer.as_slice()),
        None => format!("{}{}", self.cluster_prefix, env),
      },
    };
    ctx.set_cluster_name(&cluster_name);

    if let Some(name) = ctx.get_request_header("x-override") {
      // Tests also pass names that are not declared, so a miss is expected here.
      let _ = ctx.set_route_action_override(&buffer_to_string(name));
    }

    if let Some(timeout_ms) = read_u64_header(ctx, "x-timeout-ms") {
      ctx.set_timeout(Duration::from_millis(timeout_ms));
    }
    if let Some(idle_timeout_ms) = read_u64_header(ctx, "x-idle-timeout-ms") {
      ctx.set_idle_timeout(Duration::from_millis(idle_timeout_ms));
    }
    if let Some(limit) = read_u64_header(ctx, "x-buffer-limit") {
      ctx.set_request_body_buffer_limit(limit);
    }
    let priority = ctx
      .get_request_header("x-priority")
      .map(|buffer| match buffer.as_slice() {
        b"high" => ResourcePriority::High,
        _ => ResourcePriority::Default,
      });
    if let Some(priority) = priority {
      ctx.set_priority(priority);
    }
    true
  }
}
