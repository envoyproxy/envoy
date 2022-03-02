#include "source/extensions/bootstrap/internal_listener_registry/internal_listener_registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListenerRegistry {

namespace {

// The tls slot is not initialized yet.
TEST(TlsInternalListenerRegistryTest, TlsSlotNotInitialized) {
  TlsInternalListenerRegistry registry;
  ASSERT_EQ(nullptr, registry.getLocalRegistry());
}

} // namespace
} // namespace InternalListenerRegistry
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
