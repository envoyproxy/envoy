#include "source/extensions/upstreams/http/config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

class ConfigTest : public ::testing::Test {
public:
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions options_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(ConfigTest, Basic) {
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
  EXPECT_FALSE(config.use_downstream_protocol_);
  EXPECT_FALSE(config.use_http2_);
}

TEST_F(ConfigTest, Downstream) {
  options_.mutable_use_downstream_protocol_config();
  {
    ProtocolOptionsConfigImpl config(options_, validation_visitor_);
    EXPECT_TRUE(config.use_downstream_protocol_);
    EXPECT_FALSE(config.use_http2_);
  }

  options_.mutable_use_downstream_protocol_config()->mutable_http2_protocol_options();
  {
    ProtocolOptionsConfigImpl config(options_, validation_visitor_);
    EXPECT_TRUE(config.use_downstream_protocol_);
    EXPECT_TRUE(config.use_http2_);
  }
}

TEST(FactoryTest, EmptyProto) {
  ProtocolOptionsConfigFactory factory;
  EXPECT_TRUE(factory.createEmptyConfigProto() != nullptr);
}

TEST_F(ConfigTest, Auto) {
  options_.mutable_auto_config();
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
  EXPECT_FALSE(config.use_downstream_protocol_);
  EXPECT_TRUE(config.use_http2_);
  EXPECT_FALSE(config.use_http3_);
  EXPECT_TRUE(config.use_alpn_);
}

TEST_F(ConfigTest, AutoHttp3) {
  options_.mutable_auto_config();
  options_.mutable_auto_config()->mutable_http3_protocol_options();
  options_.mutable_auto_config()->mutable_alternate_protocols_cache_options();
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
  EXPECT_TRUE(config.use_http2_);
  EXPECT_TRUE(config.use_http3_);
  EXPECT_TRUE(config.use_alpn_);
}

TEST_F(ConfigTest, AutoHttp3NoCache) {
  options_.mutable_auto_config();
  options_.mutable_auto_config()->mutable_http3_protocol_options();
  EXPECT_THROW_WITH_MESSAGE(
      ProtocolOptionsConfigImpl config(options_, validation_visitor_), EnvoyException,
      "alternate protocols cache must be configured when HTTP/3 is enabled with auto_config");
}

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
