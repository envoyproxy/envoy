//! Integration test module for early header mutation dynamic modules.
//!
//! This module registers early header mutation configurations through the `early_header_mutation:`
//! arm of `declare_all_init_functions!`. The configurations exercise the full context ABI surface:
//! header reads (single value, indexed multi-value, count, bulk iteration), header mutation (set,
//! add, remove), stream info attributes (string, int, bool), dynamic metadata (string, number,
//! bool) and filter state bytes.
//!
//! Two names differ only in their chain-continuation return value so the integration test can
//! observe that `false` stops the chain while `true` lets the next extension run. A third name
//! counts invocations through an atomic, exercising the `Send + Sync` requirement of a single
//! configuration shared by every worker thread. An unknown name returns `None` so the config test
//! can assert rejection.

use envoy_proxy_dynamic_modules_rust_sdk::early_header_mutation::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::atomic::{AtomicU64, Ordering};

declare_all_init_functions!(
  init,
  early_header_mutation: new_early_header_mutation_config_fn
);

fn init() -> bool {
  true
}

/// Early header mutation factory: dispatches on `early_header_mutation_name`. Returning `None` for
/// an unknown name causes Envoy to reject the configuration at config load time.
fn new_early_header_mutation_config_fn(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn EarlyHeaderMutationConfig>> {
  let marker = String::from_utf8_lossy(config).into_owned();
  match name {
    "test_mutation" => Some(Box::new(TestMutation {
      marker,
      continue_chain: true,
    })),
    "stop_chain" => Some(Box::new(TestMutation {
      marker,
      continue_chain: false,
    })),
    "counting" => Some(Box::new(CountingMutation {
      calls: AtomicU64::new(0),
    })),
    _ => None,
  }
}

struct TestMutation {
  marker: String,
  continue_chain: bool,
}

impl EarlyHeaderMutationConfig for TestMutation {
  fn mutate(&self, ctx: &mut EarlyHeaderMutationContext) -> bool {
    // Header read: echo a single value back under a different key.
    if let Some(value) = ctx.get_header("x-test-input") {
      let echoed = value.as_slice().to_vec();
      ctx.set_header("x-dynamic-module-echo", &echoed);
    }

    // Header read: count and bulk iteration. The key list is sorted so the assertion is stable
    // regardless of header map ordering.
    let count = ctx.get_headers_count();
    ctx.set_header(
      "x-dynamic-module-header-count",
      count.to_string().as_bytes(),
    );
    let mut keys: Vec<String> = ctx
      .get_all_headers()
      .iter()
      .map(|(k, _)| String::from_utf8_lossy(k.as_slice()).into_owned())
      .collect();
    keys.sort();
    ctx.set_header("x-dynamic-module-keys", keys.join(",").as_bytes());

    // Header mutation: add builds a multi-value header, which get_header_value then reports.
    ctx.add_header("x-dynamic-module-added", b"one");
    ctx.add_header("x-dynamic-module-added", b"two");
    if let Some((second, total)) = ctx.get_header_value("x-dynamic-module-added", 1) {
      let observed = format!("{}:{}", String::from_utf8_lossy(second.as_slice()), total);
      ctx.set_header("x-dynamic-module-multi", observed.as_bytes());
    }

    // Header mutation: remove.
    ctx.remove_header("x-remove-me");
    ctx.set_header(
      "x-dynamic-module-removed",
      if ctx.get_header("x-remove-me").is_none() {
        b"yes"
      } else {
        b"no"
      },
    );

    // Stream info: attributes.
    if let Some(protocol) =
      ctx.get_attribute_string(abi::envoy_dynamic_module_type_attribute_id::RequestProtocol)
    {
      let protocol = protocol.as_slice().to_vec();
      ctx.set_header("x-dynamic-module-protocol", &protocol);
    }
    // ConnectionId is populated at connection establishment, so it is available this early.
    if ctx
      .get_attribute_int(abi::envoy_dynamic_module_type_attribute_id::ConnectionId)
      .is_some()
    {
      ctx.set_header("x-dynamic-module-connection-id-present", b"yes");
    }
    if let Some(mtls) =
      ctx.get_attribute_bool(abi::envoy_dynamic_module_type_attribute_id::ConnectionMtls)
    {
      ctx.set_header("x-dynamic-module-mtls", if mtls { b"yes" } else { b"no" });
    }

    // Stream info: the route and response attributes are not populated this early, so this getter
    // must report absence rather than a stale value.
    ctx.set_header(
      "x-dynamic-module-response-code-absent",
      if ctx
        .get_attribute_int(abi::envoy_dynamic_module_type_attribute_id::ResponseCode)
        .is_none()
      {
        b"yes"
      } else {
        b"no"
      },
    );

    // Stream info: dynamic metadata and filter state are readable but not writable here.
    if let Some(value) = ctx.get_dynamic_metadata("envoy.test.early", "key") {
      let value = value.as_slice().to_vec();
      ctx.set_header("x-dynamic-module-metadata", &value);
    }
    if let Some(value) = ctx.get_dynamic_metadata_number("envoy.test.early", "number") {
      ctx.set_header(
        "x-dynamic-module-metadata-number",
        value.to_string().as_bytes(),
      );
    }
    if let Some(value) = ctx.get_dynamic_metadata_bool("envoy.test.early", "flag") {
      ctx.set_header(
        "x-dynamic-module-metadata-bool",
        if value { b"yes" } else { b"no" },
      );
    }
    if let Some(value) = ctx.get_filter_state_bytes("envoy.test.early.state") {
      let value = value.as_slice().to_vec();
      ctx.set_header("x-dynamic-module-filter-state", &value);
    }

    // The configuration bytes reach the module through the factory.
    ctx.set_header("x-dynamic-module-marker", self.marker.as_bytes());

    self.continue_chain
  }
}

/// Exercises the `Send + Sync` requirement: one instance is shared by every worker thread, so the
/// only mutable state it keeps is an atomic.
struct CountingMutation {
  calls: AtomicU64,
}

impl EarlyHeaderMutationConfig for CountingMutation {
  fn mutate(&self, ctx: &mut EarlyHeaderMutationContext) -> bool {
    let calls = self.calls.fetch_add(1, Ordering::Relaxed) + 1;
    ctx.set_header("x-dynamic-module-calls", calls.to_string().as_bytes());
    true
  }
}
