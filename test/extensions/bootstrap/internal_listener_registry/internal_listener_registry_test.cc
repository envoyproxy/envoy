#include "source/extensions/bootstrap/internal_listener_registry/internal_listener_registry.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/network_utility.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListenerRegistry {

namespace {

TEST(TlsInternalListenerRegistryTest, TlsSlotNotInitialized) {
  TlsInternalListenerRegistry registry;
  ASSERT_EQ(nullptr, registry.getLocalRegistry());
}

} // namespace
} // namespace InternalListenerRegistry
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
