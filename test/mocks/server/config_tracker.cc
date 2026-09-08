#include "config_tracker.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;

MockConfigTracker::MockConfigTracker() {
  ON_CALL(*this, add_(_, _))
      .WillByDefault(Invoke([this](const std::string& key, Cb callback) -> EntryOwner* {
        EXPECT_FALSE(config_tracker_callbacks_.contains(key));
        config_tracker_callbacks_[key] = callback;
        return new MockEntryOwner();
      }));
}

MockConfigTracker::~MockConfigTracker() = default;

} // namespace Server
} // namespace Envoy
