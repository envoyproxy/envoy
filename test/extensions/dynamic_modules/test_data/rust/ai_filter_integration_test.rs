//! Integration test module for AI filter dynamic modules.
//!
//! It registers AI filters through the `ai_filter:` arm of `declare_all_init_functions!`, with the
//! same observable behavior as the C test module `ai_filter.c`, so the integration test runs the
//! same assertions against both. An unknown name returns `None` so Envoy rejects the configuration.

use envoy_proxy_dynamic_modules_rust_sdk::ai_filter::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_all_init_functions!(init, ai_filter: new_ai_filter_config_fn);

fn init() -> bool {
  true
}

fn new_ai_filter_config_fn(name: &str, _config: &[u8]) -> Option<Box<dyn AiFilterConfig>> {
  match name {
    "rewrite" | "local_reply" | "error" | "fail_new" => Some(Box::new(TestAiFilterConfig {
      name: name.to_string(),
    })),
    _ => None,
  }
}

struct TestAiFilterConfig {
  name: String,
}

impl AiFilterConfig for TestAiFilterConfig {
  fn new_ai_filter(&self, _envoy_filter: &mut EnvoyAiFilter) -> Option<Box<dyn AiFilter>> {
    Some(match self.name.as_str() {
      "rewrite" => Box::new(RewriteFilter),
      "local_reply" => Box::new(LocalReplyFilter),
      "fail_new" => panic!("new_ai_filter failed"),
      _ => Box::new(ErrorFilter),
    })
  }
}

type Status = abi::envoy_dynamic_module_type_on_ai_filter_request_status;

/// Reads and rewrites the payload, headers and filter state, failing the request if any write is
/// rejected.
struct RewriteFilter;

impl RewriteFilter {
  fn rewrite(envoy_filter: &mut EnvoyAiFilter) -> Result<(), AiFilterJsonError> {
    if let Some(protocol) = envoy_filter.get_request_protocol() {
      let protocol = protocol.as_slice().to_vec();
      envoy_filter.set_request_header("x-protocol", &protocol);
    }
    let model = envoy_filter.get_request_json("/model")?;
    envoy_filter.set_request_header("x-model", model.as_bytes());

    envoy_filter.set_request_json("/model", r#""rewritten""#)?;
    envoy_filter.set_request_json("/metadata", r#"{"tagged":true}"#)?;
    envoy_filter.set_request_json("/messages/-", r#"{"role":"system","content":"appended"}"#)?;
    envoy_filter.remove_request_json("/temperature")?;

    if let Some(tenant) = envoy_filter.get_request_header("x-tenant") {
      let tenant = tenant.as_slice().to_vec();
      envoy_filter.set_filter_state_bytes("tenant", &tenant);
    }
    if let Some(tenant) = envoy_filter.get_filter_state_bytes("tenant") {
      let tenant = tenant.as_slice().to_vec();
      envoy_filter.set_request_header("x-tenant-echo", &tenant);
    }
    envoy_filter.remove_request_header("x-remove-me");
    Ok(())
  }
}

impl AiFilter for RewriteFilter {
  fn on_request(&mut self, envoy_filter: &mut EnvoyAiFilter) -> Status {
    match Self::rewrite(envoy_filter) {
      Ok(()) => Status::Continue,
      Err(_) => Status::Error,
    }
  }
}

struct LocalReplyFilter;

impl AiFilter for LocalReplyFilter {
  fn on_request(&mut self, envoy_filter: &mut EnvoyAiFilter) -> Status {
    envoy_filter.send_local_reply(403, b"blocked");
    Status::Continue
  }
}

struct ErrorFilter;

impl AiFilter for ErrorFilter {
  fn on_request(&mut self, _envoy_filter: &mut EnvoyAiFilter) -> Status {
    Status::Error
  }
}
