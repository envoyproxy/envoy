//! Example route specifier that shadows or applies its own routing.
//!
//! A module receives the route Envoy matched through its context, so it can shadow a routing
//! decision without any support from Envoy. In a dry run this module compares the cluster it would
//! route to against the matched route, counts whether they agree, and leaves routing unchanged. In
//! a wet run it applies the decision instead. An operator runs a dry run to gain confidence, then
//! switches the same module to a wet run.
//!
//! The configuration is a `google.protobuf.StringValue` of the form `mode cluster`, where `mode` is
//! `dry-run` or `wet-run` and `cluster` is the upstream the module would route to.

use envoy_proxy_dynamic_modules_rust_sdk::route_specifier::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Arc;

declare_all_init_functions!(init, route_specifier: new_shadow_config_fn);

fn init() -> bool {
  true
}

fn new_shadow_config_fn(
  name: &str,
  config: &[u8],
  envoy_config: Arc<dyn EnvoyRouteSpecifierConfig>,
) -> Option<Box<dyn RouteSpecifierConfig>> {
  match name {
    "shadow_example" => {
      // The configuration names the mode and the cluster the module would route to. An unknown
      // mode or an empty cluster returns None, which Envoy rejects at config load time.
      let config = String::from_utf8_lossy(config);
      let (mode, cluster) = config.split_once(' ')?;
      let cluster = cluster.trim();
      if cluster.is_empty() {
        return None;
      }
      let dry_run = match mode.trim() {
        "dry-run" => true,
        "wet-run" => false,
        _ => return None,
      };
      // A dry run counts whether its decision agrees with the matched route on counters defined
      // here, while stat creation is allowed. A wet run applies the decision and needs no counters.
      let (matches, mismatches) = if dry_run {
        (
          envoy_config.define_counter("shadow_match").ok(),
          envoy_config.define_counter("shadow_mismatch").ok(),
        )
      } else {
        (None, None)
      };
      Some(Box::new(ShadowExampleConfig {
        envoy_config,
        dry_run,
        cluster: cluster.to_owned(),
        matches,
        mismatches,
      }))
    },
    _ => None,
  }
}

/// Routes to a fixed cluster, either shadowing the decision or applying it.
struct ShadowExampleConfig {
  envoy_config: Arc<dyn EnvoyRouteSpecifierConfig>,
  dry_run: bool,
  cluster: String,
  matches: Option<EnvoyCounterId>,
  mismatches: Option<EnvoyCounterId>,
}

impl RouteSpecifierConfig for ShadowExampleConfig {
  fn on_route(&self, ctx: &mut RouteSpecifierContext) -> RouteDecision {
    if !self.dry_run {
      // A wet run applies the decision, so the request routes to the module's cluster. A cluster
      // the route cannot accept is reported as an error rather than as a silent no-op.
      if ctx.set_cluster_name(&self.cluster) {
        return RouteDecision::Override;
      }
      return RouteDecision::Error;
    }
    // A dry run reads the matched route from the context and counts whether the decision agrees
    // with it, leaving routing unchanged by passing through. A matched route without a cluster,
    // such as a direct response or a redirect, never agrees.
    let agrees = ctx
      .input_route_cluster_name()
      .is_some_and(|matched| matched.as_slice() == self.cluster.as_bytes());
    let counter = if agrees {
      self.matches
    } else {
      self.mismatches
    };
    if let Some(id) = counter {
      let _ = self.envoy_config.increment_counter(id, 1);
    }
    RouteDecision::PassThrough
  }
}
