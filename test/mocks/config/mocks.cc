#include "test/mocks/config/mocks.h"

#include "envoy/config/core/v3/config_source.pb.h"

#include "test/test_common/utility.h"

using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Config {

MockSubscriptionFactory::MockSubscriptionFactory() {
  ON_CALL(*this, subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillByDefault(Invoke([this](const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                   Stats::Scope&, SubscriptionCallbacks& callbacks,
                                   OpaqueResourceDecoderSharedPtr,
                                   const SubscriptionOptions&) -> SubscriptionPtr {
        auto ret = std::make_unique<NiceMock<MockSubscription>>();
        subscription_ = ret.get();
        callbacks_ = &callbacks;
        return ret;
      }));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockSubscriptionFactory::~MockSubscriptionFactory() = default;

MockGrpcMuxWatch::MockGrpcMuxWatch() = default;
MockGrpcMuxWatch::~MockGrpcMuxWatch() { cancel(); }

MockGrpcMux::MockGrpcMux() = default;
MockGrpcMux::~MockGrpcMux() = default;

MockGrpcStreamCallbacks::MockGrpcStreamCallbacks() = default;
MockGrpcStreamCallbacks::~MockGrpcStreamCallbacks() = default;

MockSubscriptionCallbacks::MockSubscriptionCallbacks() = default;
MockSubscriptionCallbacks::~MockSubscriptionCallbacks() = default;

MockOpaqueResourceDecoder::MockOpaqueResourceDecoder() = default;
MockOpaqueResourceDecoder::~MockOpaqueResourceDecoder() = default;

MockUntypedConfigUpdateCallbacks::MockUntypedConfigUpdateCallbacks() = default;
MockUntypedConfigUpdateCallbacks::~MockUntypedConfigUpdateCallbacks() = default;

MockTypedFactory::~MockTypedFactory() = default;

MockContextProvider::MockContextProvider() {
  ON_CALL(*this, addDynamicContextUpdateCallback(_))
      .WillByDefault(Invoke([this](UpdateNotificationCb update_cb) -> Common::CallbackHandlePtr {
        return update_cb_handler_.add(update_cb);
      }));
  ON_CALL(*this, nodeContext())
      .WillByDefault(ReturnRef(xds::core::v3::ContextParams::default_instance()));
  ON_CALL(*this, dynamicContext(_))
      .WillByDefault(ReturnRef(xds::core::v3::ContextParams::default_instance()));
}

MockContextProvider::~MockContextProvider() = default;

} // namespace Config
} // namespace Envoy
