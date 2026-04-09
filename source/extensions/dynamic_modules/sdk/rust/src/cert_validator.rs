//! Certificate validator support for dynamic modules.
//!
//! This module provides traits and types for implementing custom TLS certificate validators
//! as dynamic modules. Certificate validators are used during TLS handshakes to verify
//! the peer's certificate chain.
//!
//! # Example
//!
//! ```
//! use envoy_proxy_dynamic_modules_rust_sdk::cert_validator::*;
//! use envoy_proxy_dynamic_modules_rust_sdk::*;
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
//!     _envoy_cert_validator: &EnvoyCertValidator,
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

use crate::{abi, bytes_to_module_buffer, EnvoyBuffer};

/// Wrapper around the Envoy cert validator config pointer, providing access to
/// Envoy-side operations such as filter state during certificate validation.
///
/// This is passed to [`CertValidatorConfig::do_verify_cert_chain`] and is only valid for
/// the duration of that call.
pub struct EnvoyCertValidator {
  raw: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
}

impl EnvoyCertValidator {
  /// Create a new `EnvoyCertValidator` from the raw Envoy pointer.
  pub(crate) fn new(raw: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr) -> Self {
    Self { raw }
  }

  /// Set a string value in the connection's filter state with Connection life span.
  ///
  /// Returns true if the operation was successful, false otherwise (e.g. no connection
  /// context available or the key already exists and is read-only).
  pub fn set_filter_state(&self, key: &[u8], value: &[u8]) -> bool {
    unsafe {
      abi::envoy_dynamic_module_callback_cert_validator_set_filter_state(
        self.raw,
        bytes_to_module_buffer(key),
        bytes_to_module_buffer(value),
      )
    }
  }

  /// Get a string value from the connection's filter state.
  ///
  /// Returns `None` if the key is not found or no connection context is available.
  pub fn get_filter_state<'a>(&'a self, key: &[u8]) -> Option<EnvoyBuffer<'a>> {
    let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: std::ptr::null(),
      length: 0,
    };
    let success = unsafe {
      abi::envoy_dynamic_module_callback_cert_validator_get_filter_state(
        self.raw,
        bytes_to_module_buffer(key),
        &mut result as *mut _ as *mut _,
      )
    };
    if success && !result.ptr.is_null() && result.length > 0 {
      Some(unsafe { EnvoyBuffer::new_from_raw(result.ptr as *const _, result.length) })
    } else {
      None
    }
  }
}

/// The result of a certificate chain validation.
pub struct ValidationResult {
  /// The overall validation status.
  pub status: ValidationStatus,
  /// The detailed client validation status.
  pub detailed_status: ClientValidationStatus,
  /// Optional TLS alert code to send on failure.
  pub tls_alert: Option<u8>,
  /// Optional error details string. If set, the SDK will pass it to Envoy via a callback so
  /// that the module does not need to manage the string's lifetime across the FFI boundary.
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
}

impl From<&ValidationResult> for abi::envoy_dynamic_module_type_cert_validator_validation_result {
  fn from(result: &ValidationResult) -> Self {
    let status = match result.status {
      ValidationStatus::Successful => {
        abi::envoy_dynamic_module_type_cert_validator_validation_status::Successful
      },
      ValidationStatus::Failed => {
        abi::envoy_dynamic_module_type_cert_validator_validation_status::Failed
      },
    };

    let detailed_status = match result.detailed_status {
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

    let (has_tls_alert, tls_alert) = match result.tls_alert {
      Some(alert) => (true, alert),
      None => (false, 0),
    };

    abi::envoy_dynamic_module_type_cert_validator_validation_result {
      status,
      detailed_status,
      tls_alert,
      has_tls_alert,
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
pub trait CertValidatorConfig: Send + Sync {
  /// Verify a certificate chain.
  ///
  /// Called during a TLS handshake to validate the peer's certificate chain.
  /// Each certificate in `certs` is DER-encoded, with the first entry being the leaf certificate.
  ///
  /// The `envoy_cert_validator` provides access to Envoy-side operations such as reading and
  /// writing filter state on the connection. It is only valid for the duration of this call.
  ///
  /// # Arguments
  /// * `envoy_cert_validator` - The Envoy cert validator handle for accessing filter state.
  /// * `certs` - Slice of DER-encoded certificates. The first entry is the leaf certificate.
  /// * `host_name` - The SNI host name for validation.
  /// * `is_server` - True if validating client certificates on the server side.
  fn do_verify_cert_chain(
    &self,
    envoy_cert_validator: &EnvoyCertValidator,
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

// =============================================================================
// FFI trampolines
// =============================================================================

use crate::{drop_wrapped_c_void_ptr, wrap_into_c_void_ptr, NEW_CERT_VALIDATOR_CONFIG_FUNCTION};

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cert_validator_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_cert_validator_config_module_ptr {
  let name_str = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
    name.ptr as *const _,
    name.length,
  ));
  let config_slice = std::slice::from_raw_parts(config.ptr as *const _, config.length);
  let new_config_fn = NEW_CERT_VALIDATOR_CONFIG_FUNCTION
    .get()
    .expect("NEW_CERT_VALIDATOR_CONFIG_FUNCTION must be set");
  match new_config_fn(name_str, config_slice) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cert_validator_config_destroy(
  config_ptr: abi::envoy_dynamic_module_type_cert_validator_config_module_ptr,
) {
  drop_wrapped_c_void_ptr!(config_ptr, CertValidatorConfig);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cert_validator_do_verify_cert_chain(
  config_envoy_ptr: abi::envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
  config_module_ptr: abi::envoy_dynamic_module_type_cert_validator_config_module_ptr,
  certs: *mut abi::envoy_dynamic_module_type_envoy_buffer,
  certs_count: usize,
  host_name: abi::envoy_dynamic_module_type_envoy_buffer,
  is_server: bool,
) -> abi::envoy_dynamic_module_type_cert_validator_validation_result {
  let config = {
    let raw = config_module_ptr as *const *const dyn CertValidatorConfig;
    &**raw
  };

  let envoy_cert_validator = EnvoyCertValidator::new(config_envoy_ptr);

  let cert_buffers = std::slice::from_raw_parts(certs, certs_count);
  let cert_slices: Vec<&[u8]> = cert_buffers
    .iter()
    .map(|buf| std::slice::from_raw_parts(buf.ptr as *const u8, buf.length))
    .collect();

  let host_name_str = std::str::from_utf8_unchecked(std::slice::from_raw_parts(
    host_name.ptr as *const _,
    host_name.length,
  ));

  let result = config.do_verify_cert_chain(
    &envoy_cert_validator,
    &cert_slices,
    host_name_str,
    is_server,
  );

  // If the module provided error details, pass them to Envoy via the callback.
  // Envoy copies the buffer immediately, so the string only needs to live until the call returns.
  if let Some(ref error) = result.error_details {
    let error_buf = abi::envoy_dynamic_module_type_module_buffer {
      ptr: error.as_ptr() as *const _,
      length: error.len(),
    };
    abi::envoy_dynamic_module_callback_cert_validator_set_error_details(
      config_envoy_ptr,
      error_buf,
    );
  }

  abi::envoy_dynamic_module_type_cert_validator_validation_result::from(&result)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cert_validator_get_ssl_verify_mode(
  config_module_ptr: abi::envoy_dynamic_module_type_cert_validator_config_module_ptr,
  handshaker_provides_certificates: bool,
) -> std::os::raw::c_int {
  let config = {
    let raw = config_module_ptr as *const *const dyn CertValidatorConfig;
    &**raw
  };
  config.get_ssl_verify_mode(handshaker_provides_certificates)
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed
/// by the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_cert_validator_update_digest(
  config_module_ptr: abi::envoy_dynamic_module_type_cert_validator_config_module_ptr,
  out_data: *mut abi::envoy_dynamic_module_type_module_buffer,
) {
  let config = {
    let raw = config_module_ptr as *const *const dyn CertValidatorConfig;
    &**raw
  };
  let digest = config.update_digest();
  (*out_data).ptr = digest.as_ptr() as *const _;
  (*out_data).length = digest.len();
}
