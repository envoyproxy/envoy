#pragma once

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

/**
 * Certification validation binary result with corresponding boring SSL alert
 * and error details if the result indicates failure.
 */
typedef struct {
  envoy_status_t result;
  uint8_t tls_alert;
  const char* error_details;
} envoy_cert_validation_result;

#ifdef __cplusplus
extern "C" { // function pointers
#endif

/**
 * Function signature for calling into platform APIs to validate certificates.
 */
typedef envoy_cert_validation_result (*envoy_validate_cert_f)(const envoy_data* certs, uint8_t size,
                                                              const char* host_name);

/**
 * Function signature for calling into platform APIs to clean up after validation is complete.
 */
typedef void (*envoy_validation_cleanup_f)();

#ifdef __cplusplus
} // function pointers
#endif

/**
 * A bag of function pointers to be registered in the platform registry.
 */
typedef struct {
  envoy_validate_cert_f validate_cert;
  envoy_validation_cleanup_f validation_cleanup;
} envoy_cert_validator;
