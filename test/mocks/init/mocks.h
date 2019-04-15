#pragma once

#include "envoy/init/manager.h"

#include "common/init/target_impl.h"
#include "common/init/watcher_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Init {

/**
 * ExpectableWatcherImpl is a real WatcherImpl, subclassed to add a mock `ready` method that you can
 * set expectations on in tests. Tests should never want a watcher with different behavior than the
 * real implementation.
 */
class ExpectableWatcherImpl : public WatcherImpl {
public:
  ExpectableWatcherImpl(absl::string_view name = "test");
  MOCK_CONST_METHOD0(ready, void());

  /**
   * Convenience method to provide a shorthand for EXPECT_CALL(watcher, ready()). Can be chained,
   * for example: watcher.expectReady().Times(0);
   */
  ::testing::internal::TypedExpectation<void()>& expectReady() const;
};

/**
 * ExpectableTargetImpl is a real TargetImpl, subclassed to add a mock `initialize` method that you
 * can set expectations on in tests. Tests should never want a target with a different behavior than
 * the real implementation.
 */
class ExpectableTargetImpl : public TargetImpl {
public:
  ExpectableTargetImpl(absl::string_view name = "test");
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

/**
 * MockManager is a typical mock. In many cases, it won't be necessary to mock any of its methods.
 * In cases where its `add` and `initialize` methods are actually called in a test, it's usually
 * sufficient to mock `add` by saving the target argument locally, and to mock `initialize` by
 * invoking the saved target with the watcher argument.
 */
struct MockManager : Manager {
  MOCK_CONST_METHOD0(state, Manager::State());
  MOCK_METHOD1(add, void(const Target&));
  MOCK_METHOD1(initialize, void(const Watcher&));
};

} // namespace Init
} // namespace Envoy
