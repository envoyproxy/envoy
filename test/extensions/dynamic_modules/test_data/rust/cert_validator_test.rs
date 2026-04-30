//! Cert validator test module. Mirror of
//! test_data/go/cert_validator_test/cert_validator_test.go.
//!
//! Registers a "test" cert validator that always returns Successful for any chain.

use envoy_proxy_dynamic_modules_rust_sdk::cert_validator::*;
use envoy_proxy_dynamic_modules_rust_sdk::declare_cert_validator_init_functions;

declare_cert_validator_init_functions!(init, new_cert_validator_config);

fn init() -> bool {
  true
}

fn new_cert_validator_config(_name: &str, _config: &[u8]) -> Option<Box<dyn CertValidatorConfig>> {
  Some(Box::new(NoOpValidator {}))
}

struct NoOpValidator {}

impl CertValidatorConfig for NoOpValidator {
  fn do_verify_cert_chain(
    &self,
    _envoy_cert_validator: &EnvoyCertValidator,
    _certs: &[&[u8]],
    _host_name: &str,
    _is_server: bool,
  ) -> ValidationResult {
    ValidationResult::successful()
  }

  fn get_ssl_verify_mode(&self, _handshaker_provides_certificates: bool) -> i32 {
    0
  }

  fn update_digest(&self) -> &[u8] {
    b"noop_cert_validator"
  }
}
