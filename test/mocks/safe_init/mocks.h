#pragma once

#include "envoy/safe_init/manager.h"

#include "common/safe_init/target_impl.h"
#include "common/safe_init/watcher_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace SafeInit {

/**
 * MockWatcher is a real WatcherImpl, subclassed to add a mock `ready` method that you can set
 * expectations on in tests. Tests should never want a watcher with different behavior than the
 * real implementation.
 */
class MockWatcher : public WatcherImpl {
public:
  MockWatcher(absl::string_view name = "mock watcher");
  MOCK_CONST_METHOD0(ready, void());

  /**
   * Convenience method to provide a shorthand for EXPECT_CALL(watcher, ready()). Can be chained,
   * for example: watcher.expectReady().Times(0);
   */
  ::testing::internal::TypedExpectation<void()>& expectReady() const;
};

/**
 * MockTarget is a real TargetImpl, subclassed to add a mock `initialize` method that you can set
 * expectations on in tests. Tests should never want a target with a different behavior than the
 * real implementation.
 */
class MockTarget : public TargetImpl {
public:
  MockTarget(absl::string_view name = "mock");
  MOCK_METHOD0(initialize, void());

  /**
   * Convenience method to provide a shorthand for EXPECT_CALL(target, initialize()). Can be
   * chained, for example: target.expectInitialize().Times(0);
   */
  ::testing::internal::TypedExpectation<void()>& expectInitialize();

  /**
   * Convenience method to provide a shorthand for expectInitialize() with mocked behavior of
   * calling `ready` immediately.
   */
  ::testing::internal::TypedExpectation<void()>& expectInitializeWillCallReady();
};

struct MockManager : Manager {
  MOCK_CONST_METHOD0(state, Manager::State());
  MOCK_METHOD1(add, void(const Target&));
  MOCK_METHOD1(initialize, void(const Watcher&));
};

} // namespace SafeInit
} // namespace Envoy
