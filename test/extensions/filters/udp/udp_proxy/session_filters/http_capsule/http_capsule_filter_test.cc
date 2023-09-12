#include "envoy/extensions/filters/udp/udp_proxy/session_filters/http_capsule/v3/http_capsule.pb.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

#include "test/extensions/filters/udp/udp_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {
namespace {

class HttpCapsuleFilterTest : public testing::Test {
public:
  void setup() {
    filter_ = std::make_unique<HttpCapsuleFilter>(server_context_.timeSource());
    filter_->initializeReadFilterCallbacks(callbacks_);
    ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  }

  NiceMock<Server::Configuration::MockFactoryContext> server_context_;
  std::unique_ptr<HttpCapsuleFilter> filter_;
  NiceMock<MockReadFilterCallbacks> callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(HttpCapsuleFilterTest, DefaultConfig) {
  setup();
}

} // namespace
} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
