#pragma once

#include <cstdint>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/**
 * Determines of a request was successful based on response headers.
 */
class ResponseEvaluator {
public:
  virtual ~ResponseEvaluator() = default;

  /**
   * Returns true if the provided HTTP code constitutes a success.
   */
  virtual bool isHttpSuccess(uint64_t code) const PURE;

  /**
   * Returns true if the provided gRPC status counts constitutes a success.
   */
  virtual bool isGrpcSuccess(uint32_t status) const PURE;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
