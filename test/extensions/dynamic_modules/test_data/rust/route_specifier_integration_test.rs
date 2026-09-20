//! Integration test module for route specifier dynamic modules.
//!
//! This module registers a route specifier through the `route_specifier:` arm of
//! `declare_all_init_functions!`. The decision is driven entirely by request headers so the test
//! can control every branch, and it exercises the full route specifier context ABI surface:
//! request headers, stream info attributes, dynamic metadata, filter state, the resolved route,
//! the decision setters, and the configuration metrics recorded on each decision.
//!
//! The headers the module reads are:
//!   `x-decision`       `override`, `select-template`, `no-route` or `error`. Without it the
//!                      module leaves the resolved route in place.
//!   `x-template`       the identifier of the route template to select.
//!   `x-cluster`        the upstream cluster to route to.
//!   `x-timeout-ms`     the route timeout to set, in milliseconds.
//!   `x-idle-timeout-ms` the stream idle timeout to set, in milliseconds.
//!   `x-max-stream-duration-ms` the maximum stream duration to set, in milliseconds.
//!   `x-buffer-limit`   the request body buffer limit to set, in bytes.
//!   `x-priority`       `high` or `default`, the upstream resource priority to set.
//!   `x-not-found-code` the status code to reply with when the selected cluster does not exist.
//!   `x-override`       the name of the route action override to select.
//!   `x-set-path`       the path of the request sent upstream.
//!   `x-set-host`       the authority of the request sent upstream.
//!   `x-append-action`  `append`, `add-if-absent`, `overwrite` or `overwrite-if-exists`, how an
//!                      added header combines with one of the same name. Defaults to `overwrite`.
//!   `x-add-request-header`  a `key=value` pair added to the request sent upstream.
//!   `x-remove-request-header` a header removed from the request sent upstream.
//!   `x-add-response-header` a `key=value` pair added to the response.
//!   `x-remove-response-header` a header removed from the response.
//!   `x-filter-disabled` the name of an HTTP filter to disable for the request.
//!   `x-stop-chain`     stops the route specifiers configured after this one.
//!   `x-continue-chain` continues the route specifiers configured after this one.
//!   `x-route-meta-string` a string value set as route metadata under `envoy.test.route`.
//!   `x-route-meta-number` a number value set as route metadata under `envoy.test.route`.
//!   `x-route-meta-bool`   a bool value set as route metadata under `envoy.test.route`.
//!   `x-route-typed-meta`  a serialized `google.protobuf.Any` set as typed route metadata. A value
//!                         of `unparsable` is not a valid Any.
//!   `x-metric-op`      `record` records the gauge and histogram, and `errors` drives every metric
//!                      error path. Both are ignored by the decision.
//!   `x-echo`           the name of a read accessor whose value is returned in the `x-echo-result`
//!                      response header, so that a test can assert on what the module read across
//!                      the ABI boundary.
//!   `x-query-cluster`  the cluster whose host counts the `cluster-host-count` echo reads.
//!   `x-query-priority` the priority the `cluster-host-count` echo reads, defaulting to 0.
//!
//! A scalar header holding `max` selects the largest value the SDK type can express, which
//! exercises the saturating conversion at the ABI boundary.

use envoy_proxy_dynamic_modules_rust_sdk::route_specifier::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Arc;
use std::time::Duration;

declare_all_init_functions!(init, route_specifier: new_route_specifier_config_fn);

fn init() -> bool {
  true
}

/// Route specifier factory function: dispatches to the config implementation based on
/// `specifier_name`. Returning `None` for an unknown name causes Envoy to reject the route
/// specifier configuration at config load time.
fn new_route_specifier_config_fn(
  name: &str,
  config: &[u8],
  envoy_config: Arc<dyn EnvoyRouteSpecifierConfig>,
) -> Option<Box<dyn RouteSpecifierConfig>> {
  match name {
    "test_route_specifier" => {
      // Metrics are defined once here while stat creation is still allowed, and recorded on every
      // decision so a test can observe both the scalar and the labeled record paths.
      let decisions_total_id = envoy_config.define_counter("decisions_total").ok();
      let decisions_by_template_id = envoy_config
        .define_counter_vec("decisions_by_template", &["template"])
        .ok();
      let shadow_results_id = envoy_config
        .define_counter_vec("shadow_results", &["outcome"])
        .ok();
      // A gauge and a histogram, each with a scalar and a labeled form, are defined here so the
      // gauge and histogram record paths can be driven per request through `x-metric-op`.
      let in_flight_id = envoy_config.define_gauge("in_flight").ok();
      let in_flight_by_template_id = envoy_config
        .define_gauge_vec("in_flight_by_template", &["template"])
        .ok();
      let decision_micros_id = envoy_config.define_histogram("decision_micros").ok();
      let decision_micros_by_template_id = envoy_config
        .define_histogram_vec("decision_micros_by_template", &["template"])
        .ok();
      // A second labeled gauge and histogram give `drive_metric_errors` a labeled id whose scalar
      // form is absent, matching how the counter uses the labeled `shadow_results` for that error.
      let in_flight_by_outcome_id = envoy_config
        .define_gauge_vec("in_flight_by_outcome", &["outcome"])
        .ok();
      let decision_micros_by_outcome_id = envoy_config
        .define_histogram_vec("decision_micros_by_outcome", &["outcome"])
        .ok();
      // Register a route template from a serialized Route, so a test can exercise a module declared
      // route. The bytes encode Route { match { prefix "/" } route { cluster "canary" } }.
      const REGISTERED_ROUTE: &[u8] = &[
        0x0a, 0x03, 0x0a, 0x01, 0x2f, 0x12, 0x08, 0x0a, 0x06, b'c', b'a', b'n', b'a', b'r', b'y',
      ];
      // Route { match { prefix "/" } direct_response { status 200 } }, so a registered template can
      // also build a direct response route.
      const REGISTERED_DIRECT: &[u8] =
        &[0x0a, 0x03, 0x0a, 0x01, 0x2f, 0x3a, 0x03, 0x08, 0xc8, 0x01];
      // Route { route { cluster "canary" } } with no match parses but does not build.
      const UNBUILDABLE_ROUTE: &[u8] =
        &[0x12, 0x08, 0x0a, 0x06, b'c', b'a', b'n', b'a', b'r', b'y'];
      let _ = envoy_config.register_route_template("registered", REGISTERED_ROUTE);
      let _ = envoy_config.register_route_template("registered_direct", REGISTERED_DIRECT);
      // A duplicate identifier, an empty identifier, bytes that do not parse, and a Route that
      // parses but does not build are all rejected, so registration stays idempotent and safe to
      // drive from a module configuration.
      let _ = envoy_config.register_route_template("registered", REGISTERED_ROUTE);
      let _ = envoy_config.register_route_template("", REGISTERED_ROUTE);
      let _ = envoy_config.register_route_template("unparsable", &[0x0a, 0x05]);
      let _ = envoy_config.register_route_template("unbuildable", UNBUILDABLE_ROUTE);
      // A module validates its own configuration against what Envoy declares, so a template the
      // route specifier does not declare is rejected here rather than per request.
      let required_template = String::from_utf8_lossy(config).into_owned();
      if !required_template.is_empty()
        && envoy_config.template_kind(&required_template) == RouteKind::None
      {
        return None;
      }
      Some(Box::new(TestRouteSpecifierConfig {
        envoy_config,
        decisions_total_id,
        decisions_by_template_id,
        shadow_results_id,
        in_flight_id,
        in_flight_by_template_id,
        decision_micros_id,
        decision_micros_by_template_id,
        in_flight_by_outcome_id,
        decision_micros_by_outcome_id,
      }))
    },
    _ => None,
  }
}

struct TestRouteSpecifierConfig {
  envoy_config: Arc<dyn EnvoyRouteSpecifierConfig>,
  decisions_total_id: Option<EnvoyCounterId>,
  decisions_by_template_id: Option<EnvoyCounterVecId>,
  shadow_results_id: Option<EnvoyCounterVecId>,
  in_flight_id: Option<EnvoyGaugeId>,
  in_flight_by_template_id: Option<EnvoyGaugeVecId>,
  decision_micros_id: Option<EnvoyHistogramId>,
  decision_micros_by_template_id: Option<EnvoyHistogramVecId>,
  in_flight_by_outcome_id: Option<EnvoyGaugeVecId>,
  decision_micros_by_outcome_id: Option<EnvoyHistogramVecId>,
}

/// Stands in for a value the module could not read, so that the absent case is observable too.
const ABSENT: &str = "absent";

fn buffer_to_string(buffer: EnvoyBuffer) -> String {
  String::from_utf8_lossy(buffer.as_slice()).into_owned()
}

fn buffer_to_string_or_absent(buffer: Option<EnvoyBuffer>) -> String {
  buffer.map_or_else(|| ABSENT.to_owned(), buffer_to_string)
}

fn read_u64_header(ctx: &RouteSpecifierContext, key: &str) -> Option<u64> {
  let value = ctx.get_request_header(key)?;
  if value.as_slice() == b"max" {
    return Some(u64::MAX);
  }
  std::str::from_utf8(value.as_slice()).ok()?.parse().ok()
}

/// Splits a `key=value` header value, ignoring a value without a separator.
fn split_pair(value: &str) -> Option<(&str, &str)> {
  value.split_once('=')
}

/// Reads the append action the test selected for the added headers.
fn read_append_action(ctx: &RouteSpecifierContext) -> HeaderAppendAction {
  match ctx
    .get_request_header("x-append-action")
    .as_ref()
    .map(EnvoyBuffer::as_slice)
  {
    Some(b"append") => HeaderAppendAction::AppendIfExistsOrAdd,
    Some(b"add-if-absent") => HeaderAppendAction::AddIfAbsent,
    Some(b"overwrite-if-exists") => HeaderAppendAction::OverwriteIfExists,
    _ => HeaderAppendAction::OverwriteIfExistsOrAdd,
  }
}

/// Reads the accessor named by the `x-echo` header. An unknown name reads nothing so that a test
/// can tell a missing value apart from a typo in the test itself.
fn read_echoed_value(ctx: &RouteSpecifierContext, name: &[u8]) -> String {
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
    b"attribute-int" => ctx
      .get_attribute_int(AttributeId::RequestSize)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"attribute-bool" => ctx
      .get_attribute_bool(AttributeId::HealthCheck)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"dynamic-metadata" => {
      buffer_to_string_or_absent(ctx.get_dynamic_metadata_string("envoy.test", "shard"))
    },
    b"dynamic-metadata-number" => ctx
      .get_dynamic_metadata_number("envoy.test", "weight")
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"dynamic-metadata-bool" => ctx
      .get_dynamic_metadata_bool("envoy.test", "enabled")
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"filter-state" => buffer_to_string_or_absent(ctx.get_filter_state_bytes("envoy.test.state")),
    b"random-value" => ctx.get_random_value().to_string(),
    b"cluster-host-count" => {
      let cluster_name = ctx
        .get_request_header("x-query-cluster")
        .map(buffer_to_string)
        .unwrap_or_default();
      // A test can request a priority that the cluster does not have, so a miss is expected here.
      let priority = read_u64_header(ctx, "x-query-priority")
        .and_then(|value| u32::try_from(value).ok())
        .unwrap_or(0);
      ctx
        .get_cluster_host_count(&cluster_name, priority)
        .map_or_else(
          || ABSENT.to_owned(),
          |counts| format!("{}/{}/{}", counts.total, counts.healthy, counts.degraded),
        )
    },
    // Reads every free property of the resolved route in one call rather than one call each.
    b"route-bulk" => ctx.input_route().map_or_else(
      || ABSENT.to_owned(),
      |route| {
        format!(
          "{:?}/{}/{}/{}",
          route.kind,
          String::from_utf8_lossy(route.name.as_slice()),
          String::from_utf8_lossy(route.virtual_host_name.as_slice()),
          route.cluster_name.map_or_else(
            || ABSENT.to_owned(),
            |name| String::from_utf8_lossy(name.as_slice()).into_owned()
          )
        )
      },
    ),
    // Reads the properties the bulk snapshot added beyond the earlier getters.
    b"route-idle-timeout" => ctx
      .input_route()
      .and_then(|route| route.idle_timeout)
      .map_or_else(|| ABSENT.to_owned(), |value| value.as_millis().to_string()),
    b"route-max-stream-duration" => ctx
      .input_route()
      .and_then(|route| route.max_stream_duration)
      .map_or_else(|| ABSENT.to_owned(), |value| value.as_millis().to_string()),
    b"route-priority" => ctx
      .input_route()
      .and_then(|route| route.priority)
      .map_or_else(
        || ABSENT.to_owned(),
        |priority| match priority {
          ResourcePriority::High => "high".to_owned(),
          ResourcePriority::Default => "default".to_owned(),
        },
      ),
    b"route-buffer-limit" => ctx
      .input_route()
      .and_then(|route| route.request_body_buffer_limit)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"route-not-found-code" => ctx
      .input_route()
      .and_then(|route| route.cluster_not_found_response_code)
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"route-flags" => ctx.input_route().map_or_else(
      || ABSENT.to_owned(),
      |route| {
        format!(
          "meta={},mm={},hash={},rl={},mirror={}",
          route.has_metadata,
          route.has_metadata_match,
          route.has_hash_policy,
          route.has_rate_limits,
          route.request_mirror_policies_count
        )
      },
    ),
    b"route-kind" => format!("{:?}", ctx.input_route_kind()),
    b"route-name" => buffer_to_string_or_absent(ctx.input_route_name()),
    b"virtual-host-name" => buffer_to_string_or_absent(ctx.input_route_virtual_host_name()),
    b"route-cluster-name" => buffer_to_string_or_absent(ctx.input_route_cluster_name()),
    b"route-redirect-location" => buffer_to_string_or_absent(ctx.input_route_redirect_location()),
    b"route-timeout" => ctx
      .input_route_timeout()
      .map_or_else(|| ABSENT.to_owned(), |value| value.as_millis().to_string()),
    b"route-response-code" => ctx
      .input_route_response_code()
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"route-metadata" => {
      buffer_to_string_or_absent(ctx.input_route_metadata_string("envoy.test.route", "key"))
    },
    b"route-metadata-number" => ctx
      .input_route_metadata_number("envoy.test.route", "number")
      .map_or_else(|| ABSENT.to_owned(), |value| value.to_string()),
    b"selected-template" => buffer_to_string_or_absent(ctx.selected_template_id()),
    _ => ABSENT.to_owned(),
  }
}

impl TestRouteSpecifierConfig {
  // Drives the reachable metric error paths. Each result is ignored, so the request still succeeds.
  fn drive_metric_errors(&self) {
    let cfg = &self.envoy_config;
    let label: &[&str] = &["x"];
    // An unknown id is not found for both the scalar and labeled forms.
    let _ = cfg.increment_counter(EnvoyCounterId(usize::MAX), 1);
    let _ = cfg.increment_counter_vec(EnvoyCounterVecId(usize::MAX), label, 1);
    let _ = cfg.set_gauge(EnvoyGaugeId(usize::MAX), 1);
    let _ = cfg.set_gauge_vec(EnvoyGaugeVecId(usize::MAX), label, 1);
    let _ = cfg.record_histogram_value(EnvoyHistogramId(usize::MAX), 1);
    let _ = cfg.record_histogram_value_vec(EnvoyHistogramVecId(usize::MAX), label, 1);
    // A labeled record with the wrong label count is invalid.
    let wrong: &[&str] = &["too", "many"];
    if let Some(id) = self.decisions_by_template_id {
      let _ = cfg.increment_counter_vec(id, wrong, 1);
    }
    if let Some(id) = self.in_flight_by_template_id {
      let _ = cfg.set_gauge_vec(id, wrong, 1);
    }
    if let Some(id) = self.decision_micros_by_template_id {
      let _ = cfg.record_histogram_value_vec(id, wrong, 1);
    }
    // A scalar operation on a labeled id finds no scalar metric but a labeled one, so it is invalid
    // labels rather than not found.
    if let Some(id) = self.shadow_results_id {
      let _ = cfg.increment_counter(EnvoyCounterId(id.0), 1);
    }
    if let Some(id) = self.in_flight_by_outcome_id {
      let _ = cfg.set_gauge(EnvoyGaugeId(id.0), 1);
    }
    if let Some(id) = self.decision_micros_by_outcome_id {
      let _ = cfg.record_histogram_value(EnvoyHistogramId(id.0), 1);
    }
    // Defining a metric after configuration is frozen is rejected.
    let _ = cfg.define_counter("late");
    let _ = cfg.define_gauge("late");
    let _ = cfg.define_histogram("late");
  }
}

impl RouteSpecifierConfig for TestRouteSpecifierConfig {
  fn on_route(&self, ctx: &mut RouteSpecifierContext) -> RouteDecision {
    let decision = match ctx.get_request_header("x-decision") {
      Some(buffer) => match buffer.as_slice() {
        b"override" => RouteDecision::Override,
        b"select-template" => RouteDecision::SelectTemplate,
        b"no-route" => RouteDecision::NoRoute,
        b"error" => RouteDecision::Error,
        _ => RouteDecision::PassThrough,
      },
      None => RouteDecision::PassThrough,
    };

    let mut template_label = String::from("none");
    if let Some(template_id) = ctx.get_request_header("x-template") {
      let template_id = buffer_to_string(template_id);
      // Tests also pass identifiers that are not declared, so a miss is expected here.
      if ctx.select_template(&template_id) {
        template_label = template_id;
      }
    }

    if ctx.get_request_header("x-stop-chain").is_some() {
      ctx.set_chain_status(ChainStatus::StopIteration);
    }
    if ctx.get_request_header("x-continue-chain").is_some() {
      ctx.set_chain_status(ChainStatus::Continue);
    }

    if let Some(cluster_name) = ctx.get_request_header("x-cluster") {
      // Tests also pass names the allowlist rejects, so a miss is expected here.
      let _ = ctx.set_cluster_name(&buffer_to_string(cluster_name));
    }
    if let Some(timeout_ms) = read_u64_header(ctx, "x-timeout-ms") {
      ctx.set_timeout(Duration::from_millis(timeout_ms));
    }
    if let Some(idle_timeout_ms) = read_u64_header(ctx, "x-idle-timeout-ms") {
      ctx.set_idle_timeout(Duration::from_millis(idle_timeout_ms));
    }
    if let Some(duration_ms) = read_u64_header(ctx, "x-max-stream-duration-ms") {
      ctx.set_max_stream_duration(Duration::from_millis(duration_ms));
    }
    if let Some(limit) = read_u64_header(ctx, "x-buffer-limit") {
      ctx.set_request_body_buffer_limit(limit);
    }
    if let Some(code) = read_u64_header(ctx, "x-not-found-code") {
      // Tests also pass out of range codes, so a rejection is expected here.
      let _ = ctx.set_cluster_not_found_response_code(u32::try_from(code).unwrap_or(u32::MAX));
    }
    if let Some(priority) = ctx.get_request_header("x-priority") {
      ctx.set_priority(match priority.as_slice() {
        b"high" => ResourcePriority::High,
        _ => ResourcePriority::Default,
      });
    }
    if let Some(name) = ctx.get_request_header("x-override") {
      // The declaration getter is queried, then the override is applied. Applying validates the name
      // too, so a name that is not declared exercises the rejection path. Tests pass both kinds.
      let name = buffer_to_string(name);
      let _ = self.envoy_config.has_route_action_override(&name);
      let _ = ctx.set_route_action_override(&name);
    }

    if let Some(path) = ctx.get_request_header("x-set-path") {
      // Tests also pass paths Envoy rejects, so a miss is expected here.
      let _ = ctx.set_path(&buffer_to_string(path));
    }
    if let Some(host) = ctx.get_request_header("x-set-host") {
      let _ = ctx.set_host(&buffer_to_string(host));
    }
    let append_action = read_append_action(ctx);
    if let Some(pair) = ctx.get_request_header("x-add-request-header") {
      let pair = buffer_to_string(pair);
      if let Some((key, value)) = split_pair(&pair) {
        let _ = ctx.add_request_header(key, value, append_action);
      }
    }
    if let Some(key) = ctx.get_request_header("x-remove-request-header") {
      let _ = ctx.remove_request_header(&buffer_to_string(key));
    }
    if let Some(pair) = ctx.get_request_header("x-add-response-header") {
      let pair = buffer_to_string(pair);
      if let Some((key, value)) = split_pair(&pair) {
        let _ = ctx.add_response_header(key, value, append_action);
      }
    }
    if let Some(key) = ctx.get_request_header("x-remove-response-header") {
      let _ = ctx.remove_response_header(&buffer_to_string(key));
    }
    if let Some(filter_name) = ctx.get_request_header("x-filter-disabled") {
      let _ = ctx.set_filter_disabled(&buffer_to_string(filter_name), true);
    }

    // Route metadata setters, each guarded by its own header so a test can drive them in isolation.
    if let Some(value) = ctx.get_request_header("x-route-meta-string") {
      let _ =
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
      let _ = ctx.set_route_metadata_number("envoy.test.route", "number_key", value);
    }
    if let Some(value) = ctx.get_request_header("x-route-meta-bool") {
      let _ =
        ctx.set_route_metadata_bool("envoy.test.route", "bool_key", value.as_slice() == b"true");
    }
    if let Some(value) = ctx.get_request_header("x-route-typed-meta") {
      // Hand-encoded google.protobuf.Any because the test module has no protobuf dependency.
      let serialized_any: &[u8] = match value.as_slice() {
        // A length prefix with no bytes following, so the set is rejected before recording.
        b"unparsable" => &[0x0a, 0x05],
        //   0a 03 74 2f 78   field 1 (type_url) = "t/x".
        //   12 02 01 02      field 2 (value)    = 0x01 0x02.
        _ => &[0x0a, 0x03, 0x74, 0x2f, 0x78, 0x12, 0x02, 0x01, 0x02],
      };
      let _ = ctx.set_route_typed_metadata("envoy.test.typed_route", serialized_any);
    }

    // The echo runs last so that it can observe the identifier the template selection recorded.
    if let Some(name) = ctx.get_request_header("x-echo") {
      let value = match name.as_slice() {
        b"template-ids" => self.envoy_config.template_ids().join(","),
        b"shadow-mode" => self.envoy_config.is_shadow_mode().to_string(),
        name => read_echoed_value(ctx, name),
      };
      let _ = ctx.add_response_header(
        "x-echo-result",
        &value,
        HeaderAppendAction::OverwriteIfExistsOrAdd,
      );
    }

    // Gauges and histograms are exercised on request so a test can drive each record path.
    if let Some(op) = ctx.get_request_header("x-metric-op") {
      let label: &[&str] = &[template_label.as_str()];
      match op.as_slice() {
        b"record" => {
          // The gauge is set then adjusted in both directions, so it leaves a deterministic value.
          if let Some(id) = self.in_flight_id {
            let _ = self.envoy_config.set_gauge(id, 10);
            let _ = self.envoy_config.increase_gauge(id, 5);
            let _ = self.envoy_config.decrease_gauge(id, 3);
          }
          if let Some(id) = self.in_flight_by_template_id {
            let _ = self.envoy_config.set_gauge_vec(id, label, 10);
            let _ = self.envoy_config.increase_gauge_vec(id, label, 5);
            let _ = self.envoy_config.decrease_gauge_vec(id, label, 3);
          }
          if let Some(id) = self.decision_micros_id {
            let _ = self.envoy_config.record_histogram_value(id, 42);
          }
          if let Some(id) = self.decision_micros_by_template_id {
            let _ = self.envoy_config.record_histogram_value_vec(id, label, 42);
          }
        },
        b"errors" => self.drive_metric_errors(),
        _ => {},
      }
    }

    // Record the decision so an integration test can observe the values that crossed the ABI
    // boundary from the worker thread.
    if let Some(id) = self.decisions_total_id {
      let _ = self.envoy_config.increment_counter(id, 1);
    }
    if let Some(id) = self.decisions_by_template_id {
      let _ = self
        .envoy_config
        .increment_counter_vec(id, &[template_label.as_str()], 1);
    }
    decision
  }

  fn on_shadow_result(
    &self,
    _ctx: &RouteSpecifierContext,
    _decision: RouteDecision,
    failure: RouteFailure,
    mismatches: ShadowMismatches,
  ) {
    let outcome = if failure != RouteFailure::None {
      "failure"
    } else if mismatches.is_match() {
      "match"
    } else {
      "mismatch"
    };
    if let Some(id) = self.shadow_results_id {
      let _ = self.envoy_config.increment_counter_vec(id, &[outcome], 1);
    }
  }
}
