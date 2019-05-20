#include "test/mocks/config/mocks.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Config {

MockGrpcMuxWatch::MockGrpcMuxWatch() {}
MockGrpcMuxWatch::~MockGrpcMuxWatch() { cancel(); }

MockGrpcMux::MockGrpcMux() {}
MockGrpcMux::~MockGrpcMux() {}

MockGrpcStreamCallbacks::MockGrpcStreamCallbacks() {}
MockGrpcStreamCallbacks::~MockGrpcStreamCallbacks() {}

GrpcMuxWatchPtr MockGrpcMux::subscribe(const std::string& type_url,
                                       const std::set<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  return GrpcMuxWatchPtr(subscribe_(type_url, resources, callbacks));
}

MockGrpcMuxCallbacks::MockGrpcMuxCallbacks() {
  ON_CALL(*this, resourceName(testing::_))
      .WillByDefault(testing::Invoke(TestUtility::xdsResourceName));
}

MockGrpcMuxCallbacks::~MockGrpcMuxCallbacks() {}

MockMutableConfigProviderBase::MockMutableConfigProviderBase(
    std::shared_ptr<ConfigSubscriptionInstance>&& subscription,
    ConfigProvider::ConfigConstSharedPtr, Server::Configuration::FactoryContext& factory_context)
    : MutableConfigProviderBase(std::move(subscription), factory_context, ApiType::Full) {
  subscription_->bindConfigProvider(this);
}

} // namespace Config
} // namespace Envoy
