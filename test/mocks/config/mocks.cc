#include "test/mocks/config/mocks.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Config {

MockSubscriptionFactory::MockSubscriptionFactory() {
  ON_CALL(*this, subscriptionFromConfigSource(_, _, _, _))
      .WillByDefault(testing::Invoke([this](const envoy::api::v2::core::ConfigSource&,
                                            absl::string_view, Stats::Scope&,
                                            SubscriptionCallbacks& callbacks) -> SubscriptionPtr {
        auto ret = std::make_unique<testing::NiceMock<MockSubscription>>();
        subscription_ = ret.get();
        callbacks_ = &callbacks;
        return ret;
      }));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(testing::ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockSubscriptionFactory::~MockSubscriptionFactory() = default;

MockGrpcMuxWatch::MockGrpcMuxWatch() = default;
MockGrpcMuxWatch::~MockGrpcMuxWatch() { cancel(); }

MockGrpcMux::MockGrpcMux() = default;
MockGrpcMux::~MockGrpcMux() = default;

MockGrpcStreamCallbacks::MockGrpcStreamCallbacks() = default;
MockGrpcStreamCallbacks::~MockGrpcStreamCallbacks() = default;

GrpcMuxWatchPtr MockGrpcMux::subscribe(const std::string& type_url,
                                       const std::set<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  return GrpcMuxWatchPtr(subscribe_(type_url, resources, callbacks));
}

MockGrpcMuxCallbacks::MockGrpcMuxCallbacks() {
  ON_CALL(*this, resourceName(testing::_))
      .WillByDefault(testing::Invoke(TestUtility::xdsResourceName));
}

MockGrpcMuxCallbacks::~MockGrpcMuxCallbacks() = default;

} // namespace Config
} // namespace Envoy
