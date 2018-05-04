#include <chrono>

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "server/config_validation/api.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {

// Define fixture which allocates ValidationDispatcher.
class ConfigValidation : public ::testing::TestWithParam<Network::Address::IpVersion> {
public:
  ConfigValidation() {
    Event::Libevent::Global::initialize();

    validation_ = std::make_unique<Api::ValidationImpl>(std::chrono::milliseconds(1000));
    dispatcher_ = validation_->allocateDispatcher();
  }

  Event::DispatcherPtr dispatcher_;

private:
  // Using config validation API.
  std::unique_ptr<Api::ValidationImpl> validation_;
};

// Simple test which creates a connection to fake upstream client. This is to test if
// ValidationDispatcher can call createClientConnection without crashing.
TEST_P(ConfigValidation, createConnection) {
  Network::Address::InstanceConstSharedPtr address(
      Network::Test::getCanonicalLoopbackAddress(GetParam()));
  dispatcher_->createClientConnection(address, address, Network::Test::createRawBufferSocket(),
                                      nullptr);
  SUCCEED();
}

INSTANTIATE_TEST_CASE_P(IpVersions, ConfigValidation,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

} // namespace Envoy
