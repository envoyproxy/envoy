#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"

#include "test/test_common/utility.h"

namespace Envoy {
class ConfigHelper;

// Observe accepted downstream connections directly, including connections with unfinished
// handshakes.
class DownstreamConnectionTracker {
public:
  DownstreamConnectionTracker(ConfigHelper& config, Event::Dispatcher& dispatcher);
  ~DownstreamConnectionTracker();

  // Wait for filter-chain initialization to complete without advancing simulated time.
  ABSL_MUST_USE_RESULT testing::AssertionResult
  waitForConnection(uint64_t count = 1,
                    std::chrono::milliseconds timeout = std::chrono::seconds(5) * TIMEOUT_FACTOR);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace Envoy
