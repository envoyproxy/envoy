//! Input matcher integration test module. Mirror of the Go module at
//! test_data/go/matcher_integration_test/matcher_integration_test.go and the C fake at
//! test_data/c/matcher_check_headers.c.
//!
//! Registers a matcher named "header_check" that takes the header name to inspect via
//! matcher_config bytes. on_matcher_match returns true iff the named request header is
//! present with value exactly "match".

use envoy_proxy_dynamic_modules_rust_sdk::declare_matcher;
use envoy_proxy_dynamic_modules_rust_sdk::matcher::*;

struct HeaderCheckConfig {
  header_name: String,
}

impl MatcherConfig for HeaderCheckConfig {
  fn new(_name: &str, config: &[u8]) -> Result<Self, String> {
    Ok(Self {
      header_name: String::from_utf8_lossy(config).to_string(),
    })
  }

  fn on_matcher_match(&self, ctx: &MatchContext) -> bool {
    match ctx.get_request_header(&self.header_name) {
      Some(value) => value == b"match",
      None => false,
    }
  }
}

declare_matcher!(HeaderCheckConfig);
