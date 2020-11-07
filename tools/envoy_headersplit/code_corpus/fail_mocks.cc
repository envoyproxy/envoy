#include "fail_mocks.h"

// NOLINT(namespace-envoy)

#include <string>

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {

MockConfigTracker::MockConfigTracker() {
  ON_CALL(*this, add_(_, _))
      .WillByDefault(Invoke([this](const std::string& key, Cb callback) -> EntryOwner* {
        EXPECT_TRUE(config_tracker_callbacks_.find(key) == config_tracker_callbacks_.end());
        config_tracker_callbacks_[key] = callback;
        return new MockEntryOwner();
      }));
}
MockConfigTracker::~MockConfigTracker() = default;

MockListenerComponentFactory::MockListenerComponentFactory()
    : socket_(std::make_shared<NiceMock<Network::MockListenSocket>>()) {
  ON_CALL(*this, createListenSocket(_, _, _, _))
      .WillByDefault(Invoke([&](Network::Address::InstanceConstSharedPtr, Network::Socket::Type,
                                const Network::Socket::OptionsSharedPtr& options,
                                const ListenSocketCreationParams&) -> Network::SocketSharedPtr {
        if (!Network::Socket::applyOptions(options, *socket_,
                                           envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
          throw EnvoyException("MockListenerComponentFactory: Setting socket options failed");
        }
        return socket_;
      }));
}
MockListenerComponentFactory::~MockListenerComponentFactory() = default;

} // namespace Server
} // namespace Envoy
