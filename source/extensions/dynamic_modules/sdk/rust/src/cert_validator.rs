//! Certificate validator support for dynamic modules.
//!
//! This module provides traits and types for implementing custom TLS certificate validators
//! as dynamic modules. Certificate validators are used during TLS handshakes to verify
//! the peer's certificate chain.
//!
//! # Example
//!
//! ```ignore
//! use envoy_proxy_dynamic_modules_rust_sdk::*;
//! use envoy_proxy_dynamic_modules_rust_sdk::cert_validator::*;
//!
//! fn program_init() -> bool {
//!   true
//! }
//!
//! fn new_cert_validator_config(name: &str, config: &[u8]) -> Option<Box<dyn CertValidatorConfig>> {
//!   Some(Box::new(MyCertValidatorConfig {}))
//! }
//!
//! declare_cert_validator_init_functions!(program_init, new_cert_validator_config);
//!
//! struct MyCertValidatorConfig {}
//!
//! impl CertValidatorConfig for MyCertValidatorConfig {
//!   fn do_verify_cert_chain(
//!     &self,
//!     certs: &[&[u8]],
//!     host_name: &str,
//!     is_server: bool,
//!   ) -> ValidationResult {
//!     ValidationResult::successful()
//!   }
//!
//!   fn get_ssl_verify_mode(&self, handshaker_provides_certificates: bool) -> i32 {
//!     0x03 // SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
//!   }
//!
//!   fn update_digest(&self) -> &[u8] {
//!     b"my_cert_validator"
//!   }
//! }
//! ```

use crate::abi;

/// The result of a certificate chain validation.
pub struct ValidationResult {
  /// The overall validation status.
  pub status: ValidationStatus,
  /// The detailed client validation status.
  pub detailed_status: ClientValidationStatus,
  /// Optional TLS alert code to send on failure.
  pub tls_alert: Option<u8>,
  /// Optional error details string.
  pub error_details: Option<String>,
}

impl ValidationResult {
  /// Create a successful validation result.
  pub fn successful() -> Self {
    Self {
      status: ValidationStatus::Successful,
      detailed_status: ClientValidationStatus::Validated,
      tls_alert: None,
      error_details: None,
    }
  }

  /// Create a failed validation result.
  pub fn failed(
    detailed_status: ClientValidationStatus,
    tls_alert: Option<u8>,
    error_details: Option<String>,
  ) -> Self {
    Self {
      status: ValidationStatus::Failed,
      detailed_status,
      tls_alert,
      error_details,
    }
  }

  /// Convert to the ABI result type.
  pub(crate) fn to_abi(
    &self,
    error_buf: &Option<String>,
  ) -> abi::envoy_dynamic_module_type_cert_validator_validation_result {
    let status = match self.status {
      ValidationStatus::Successful => {
        abi::envoy_dynamic_module_type_cert_validator_validation_status::Successful
      },
      ValidationStatus::Failed => {
        abi::envoy_dynamic_module_type_cert_validator_validation_status::Failed
      },
    };

    let detailed_status = match self.detailed_status {
      ClientValidationStatus::NotValidated => {
        abi::envoy_dynamic_module_type_cert_validator_client_validation_status::NotValidated
      },
      ClientValidationStatus::NoClientCertificate => {
        abi::envoy_dynamic_module_type_cert_validator_client_validation_status::NoClientCertificate
      },
      ClientValidationStatus::Validated => {
        abi::envoy_dynamic_module_type_cert_validator_client_validation_status::Validated
      },
      ClientValidationStatus::Failed => {
        abi::envoy_dynamic_module_type_cert_validator_client_validation_status::Failed
      },
    };

    let (has_tls_alert, tls_alert) = match self.tls_alert {
      Some(alert) => (true, alert),
      None => (false, 0),
    };

    let error_details = match error_buf {
      Some(s) => abi::envoy_dynamic_module_type_module_buffer {
        ptr: s.as_ptr() as *const _,
        length: s.len(),
      },
      None => abi::envoy_dynamic_module_type_module_buffer {
        ptr: std::ptr::null(),
        length: 0,
      },
    };

    abi::envoy_dynamic_module_type_cert_validator_validation_result {
      status,
      detailed_status,
      tls_alert,
      has_tls_alert,
      error_details,
    }
  }
}

/// The overall validation status.
pub enum ValidationStatus {
  /// The certificate chain is valid.
  Successful,
  /// The certificate chain is invalid.
  Failed,
}

/// Detailed client validation status.
pub enum ClientValidationStatus {
  /// Client certificate was not validated.
  NotValidated,
  /// No client certificate was provided.
  NoClientCertificate,
  /// Client certificate was successfully validated.
  Validated,
  /// Client certificate validation failed.
  Failed,
}

/// Trait for implementing a certificate validator configuration.
///
/// An implementation of this trait is created once per validator configuration and shared
/// across TLS handshakes. All methods must be thread-safe.
pub trait CertValidatorConfig: Send + Sync + 'static {
  /// Verify a certificate chain.
  ///
  /// Called during a TLS handshake to validate the peer's certificate chain.
  /// Each certificate in `certs` is DER-encoded, with the first entry being the leaf certificate.
  ///
  /// # Arguments
  /// * `certs` - Slice of DER-encoded certificates. The first entry is the leaf certificate.
  /// * `host_name` - The SNI host name for validation.
  /// * `is_server` - True if validating client certificates on the server side.
  fn do_verify_cert_chain(
    &self,
    certs: &[&[u8]],
    host_name: &str,
    is_server: bool,
  ) -> ValidationResult;

  /// Get the SSL verify mode flags.
  ///
  /// Called during SSL context initialization. The return value should be a combination of
  /// SSL_VERIFY_* flags. For example, `0x03` for `SSL_VERIFY_PEER |
  /// SSL_VERIFY_FAIL_IF_NO_PEER_CERT`.
  fn get_ssl_verify_mode(&self, handshaker_provides_certificates: bool) -> i32;

  /// Get bytes to contribute to the session context hash.
  ///
  /// Returns bytes that uniquely identify this validation configuration so that configuration
  /// changes invalidate existing TLS sessions. The returned slice must remain valid for the
  /// lifetime of the config.
  fn update_digest(&self) -> &[u8];
}
