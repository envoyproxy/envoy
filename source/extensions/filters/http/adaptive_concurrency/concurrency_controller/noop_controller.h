#pragma once

#include <chrono>

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace ConcurrencyController {

/**
 * Adaptive concurrency controller that does nothing.
 */
class NoopController : public ConcurrencyController {
public:
  // ConcurrencyController.
  bool tryForwardRequest() override { return true; }
  void recordLatencySample(const std::chrono::nanoseconds&) override {}
};

} // namespace ConcurrencyController
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
