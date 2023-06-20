#include "source/extensions/bootstrap/internal_listener/internal_listener_registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

namespace {

// The tls slot is not initialized yet.
TEST(TlsInternalListenerRegistryTest, TlsSlotNotInitialized) {
  TlsInternalListenerRegistry registry;
  ASSERT_EQ(nullptr, registry.getLocalRegistry());
}

} // namespace
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
