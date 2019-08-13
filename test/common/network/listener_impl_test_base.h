#pragma once

#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {

// Captures common infrastructure needed by both ListenerImplTest and UdpListenerImplTest.
class ListenerImplTestBase : public testing::TestWithParam<Address::IpVersion> {
protected:
  ListenerImplTestBase()
      : version_(GetParam()),
        alt_address_(Network::Test::findOrCheckFreePort(
            Network::Test::getCanonicalLoopbackAddress(version_), Address::SocketType::Stream)),
        api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()) {}

  Event::DispatcherImpl& dispatcherImpl() {
    // We need access to the concrete impl type in order to instantiate a
    // Test[Udp]Listener, which instantiates a [Udp]ListenerImpl, which requires
    // a DispatcherImpl to access DispatcherImpl::base_, which is not part of
    // the Dispatcher API.
    Event::DispatcherImpl* impl = dynamic_cast<Event::DispatcherImpl*>(dispatcher_.get());
    RELEASE_ASSERT(impl, "dispatcher dynamic-cast to DispatcherImpl failed");
    return *impl;
  }

  const Address::IpVersion version_;
  const Address::InstanceConstSharedPtr alt_address_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

} // namespace Network
} // namespace Envoy
