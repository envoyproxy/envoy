#include <memory>

#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;
namespace {

TEST(SocketInterfaceAbiImplTest, IoReadyWithNullHandleIsEnvoyBug) {
  EXPECT_ENVOY_BUG(envoy_dynamic_module_callback_socket_interface_io_ready(nullptr, 1),
                   "envoy_dynamic_module_callback_socket_interface_io_ready called with a null "
                   "handle");
}

TEST(SocketInterfaceAbiImplTest, IoReadyDeleteWithNullHandleIsEnvoyBug) {
  EXPECT_ENVOY_BUG(envoy_dynamic_module_callback_socket_interface_io_ready_delete(nullptr),
                   "envoy_dynamic_module_callback_socket_interface_io_ready_delete called with a "
                   "null handle");
}

TEST(SocketInterfaceAbiImplTest, IoReadyAfterStateGoneIsNoOp) {
  auto* handle = new SocketReadyHandle(
      std::weak_ptr<SocketSharedState>(std::make_shared<SocketSharedState>()));
  // The shared state dies with the temporary shared_ptr, so the commit is a no-op.
  envoy_dynamic_module_callback_socket_interface_io_ready(handle, 1);
  envoy_dynamic_module_callback_socket_interface_io_ready_delete(handle);
}

TEST(SocketInterfaceAbiImplTest, RacingReportIsNotCleared) {
  auto state = std::make_shared<SocketSharedState>();
  SocketReadyHandle handle(state);
  handle.commit(envoy_dynamic_module_type_socket_interface_io_event_Read);
  const uint64_t sequence = state->reportSequence();
  // A report lands while the hook that consumed the earlier readiness runs.
  handle.commit(envoy_dynamic_module_type_socket_interface_io_event_Read);
  state->clearPending(Event::FileReadyType::Read, sequence);
  EXPECT_EQ(Event::FileReadyType::Read, state->pending());
  state->clearPending(Event::FileReadyType::Read, state->reportSequence());
  EXPECT_EQ(0, state->pending());
}

TEST(SocketInterfaceAbiImplTest, IoReadyOnClosedStateIsNoOp) {
  auto state = std::make_shared<SocketSharedState>();
  state->markClosed();
  auto* handle = new SocketReadyHandle(state);
  envoy_dynamic_module_callback_socket_interface_io_ready(
      handle, envoy_dynamic_module_type_socket_interface_io_event_Read);
  EXPECT_EQ(0, state->pending());
  envoy_dynamic_module_callback_socket_interface_io_ready_delete(handle);
}

} // namespace
} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
