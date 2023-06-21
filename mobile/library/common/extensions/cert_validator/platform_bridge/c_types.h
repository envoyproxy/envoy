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
