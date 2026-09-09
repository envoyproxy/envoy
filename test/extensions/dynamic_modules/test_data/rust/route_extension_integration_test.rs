//! A route extension test module for the dynamic modules integration and unit tests.
//!
//! The decision is driven entirely by request headers so the tests can exercise every path with a
//! single module.

use envoy_proxy_dynamic_modules_rust_sdk::route_extension::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_all_init_functions!(init, route_extension: new_route_extension_config_fn);

fn init() -> bool {
  true
}

fn new_route_extension_config_fn(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn RouteExtensionConfig>> {
  match name {
    "test_route_extension" => Some(Box::new(TestRouteExtensionConfig {
      default_cluster: String::from_utf8_lossy(config).into_owned(),
    })),
    _ => None,
  }
}

struct TestRouteExtensionConfig {
  default_cluster: String,
}

impl RouteExtensionConfig for TestRouteExtensionConfig {
  fn on_route(&self, ctx: &mut RouteExtensionContext) -> RouteExtensionDecision {
    // Reading the random value exercises that part of the context surface.
    let _random_value = ctx.get_random_value();

    // The "x-route-action" header selects the decision. It is found by scanning all headers so the
    // bulk header read is exercised. Without it the route is kept.
    let action = ctx
      .get_all_request_headers()
      .into_iter()
      .find(|(key, _)| key.as_slice() == b"x-route-action")
      .map(|(_, value)| String::from_utf8_lossy(value.as_slice()).into_owned());

    match action.as_deref() {
      Some("override") => {
        // Use the cluster carried in the request when present, otherwise the configured default.
        let cluster = match ctx.get_request_header("x-cluster") {
          Some(buffer) => String::from_utf8_lossy(buffer.as_slice()).into_owned(),
          None => self.default_cluster.clone(),
        };
        ctx.set_cluster_name(&cluster);
        // The "x-action-override" header selects a route action override by name when present.
        if let Some(buffer) = ctx.get_request_header("x-action-override") {
          let name = String::from_utf8_lossy(buffer.as_slice()).into_owned();
          let _ = ctx.set_route_action_override(&name);
        }
        RouteExtensionDecision::Override
      },
      Some("drop") => RouteExtensionDecision::Drop,
      _ => RouteExtensionDecision::Keep,
    }
  }
}
