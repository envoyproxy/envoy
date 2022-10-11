#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/http/header_validators/envoy_default/config.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::Protocol;

class HeaderValidatorFactoryTest : public testing::Test {
protected:
  ::Envoy::Http::HeaderValidatorPtr create(absl::string_view config_yaml, Protocol protocol) {
    auto* factory =
        Registry::FactoryRegistry<Envoy::Http::HeaderValidatorFactoryConfig>::getFactory(
            "envoy.http.header_validators.envoy_default");
    ASSERT(factory != nullptr);

    envoy::config::core::v3::TypedExtensionConfig typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    uhv_factory_ = factory->createFromProto(typed_config.typed_config(), context_);
    return uhv_factory_->create(protocol, stream_info_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  ::Envoy::Http::HeaderValidatorFactoryPtr uhv_factory_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

  static constexpr absl::string_view empty_config = R"EOF(
    name: envoy.http.header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig
)EOF";
};

TEST_F(HeaderValidatorFactoryTest, CreateHttp09) {
  auto uhv = create(empty_config, Protocol::Http10);
  EXPECT_NE(dynamic_cast<Http1HeaderValidator*>(uhv.get()), nullptr);
}

TEST_F(HeaderValidatorFactoryTest, CreateHttp1) {
  auto uhv = create(empty_config, Protocol::Http11);
  EXPECT_NE(dynamic_cast<Http1HeaderValidator*>(uhv.get()), nullptr);
}

TEST_F(HeaderValidatorFactoryTest, CreateHttp2) {
  auto uhv = create(empty_config, Protocol::Http2);
  EXPECT_NE(dynamic_cast<Http2HeaderValidator*>(uhv.get()), nullptr);
}

TEST_F(HeaderValidatorFactoryTest, CreateHttp3) {
  auto uhv = create(empty_config, Protocol::Http3);
  EXPECT_NE(dynamic_cast<Http2HeaderValidator*>(uhv.get()), nullptr);
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
