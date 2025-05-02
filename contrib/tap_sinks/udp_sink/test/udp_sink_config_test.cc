#include <memory>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/common/tap/tap.h"
#include "source/extensions/common/tap/tap_config_base.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

// Test Udp sink
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;

namespace TapCommon = Extensions::Common::Tap;

class TestConfigImpl : public TapCommon::TapConfigBaseImpl {
public:
  TestConfigImpl(const envoy::config::tap::v3::TapConfig& proto_config,
                 Extensions::Common::Tap::Sink* admin_streamer, TapCommon::SinkContext context)
      : TapCommon::TapConfigBaseImpl(std::move(proto_config), admin_streamer, context) {}
};

class UdpTapSinkConfigTest : public testing::Test {
protected:
  UdpTapSinkConfigTest() {}
  ~UdpTapSinkConfigTest() {}
};

TEST_F(UdpTapSinkConfigTest, AddTestConfigHttpContextForUdpSink) {
  const std::string tap_config_yaml =
      R"EOF(
  match:
    any_match: true
  output_config:
    sinks:
      - format: JSON_BODY_AS_STRING
        custom_sink:
          name: custom_sink_udp
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.tap_sinks.udp_sink.v3alpha.UdpSink
            udp_address:
              protocol: UDP
              address: 127.0.0.1
              port_value: 8089
)EOF";
  envoy::config::tap::v3::TapConfig tap_config;
  TestUtility::loadFromYaml(tap_config_yaml, tap_config);

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  TestConfigImpl(tap_config, nullptr, factory_context);
}

TEST_F(UdpTapSinkConfigTest, AddTestConfigTransportSocketContextForUdpSink) {
  const std::string tap_config_yaml =
      R"EOF(
  match:
    any_match: true
  output_config:
    sinks:
      - format: JSON_BODY_AS_STRING
        custom_sink:
          name: custom_sink
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.tap_sinks.udp_sink.v3alpha.UdpSink
            udp_address:
              protocol: UDP
              address: 127.0.0.1
              port_value: 8089
)EOF";
  envoy::config::tap::v3::TapConfig tap_config;
  TestUtility::loadFromYaml(tap_config_yaml, tap_config);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  TestConfigImpl(tap_config, nullptr, factory_context);
}

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
