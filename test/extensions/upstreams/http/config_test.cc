#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/extensions/upstreams/http/config.h"

#include "test/extensions/upstreams/http/config.pb.h"
#include "test/extensions/upstreams/http/config.pb.validate.h"
#include "test/mocks/http/header_validator.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {

using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;

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

namespace {

class TestHeaderValidatorFactoryConfig : public ::Envoy::Http::HeaderValidatorFactoryConfig {
public:
  std::string name() const override { return "test.upstreams.http.CustomHeaderValidator"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::upstreams::http::CustomHeaderValidator>();
  }

  ::Envoy::Http::HeaderValidatorFactoryPtr
  createFromProto(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    auto header_validator =
        std::make_unique<StrictMock<::Envoy::Http::MockHeaderValidatorFactory>>();
    EXPECT_CALL(*header_validator, create(::Envoy::Http::Protocol::Http2, _))
        .WillOnce(InvokeWithoutArgs(
            []() { return std::make_unique<StrictMock<::Envoy::Http::MockHeaderValidator>>(); }));
    return header_validator;
  }
};

// Override the default config factory such that the test can validate the UHV config proto that
// ProtocolOptions factory synthesized.
class DefaultHeaderValidatorFactoryConfigOverride
    : public ::Envoy::Http::HeaderValidatorFactoryConfig {
public:
  DefaultHeaderValidatorFactoryConfigOverride(
      ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config)
      : config_(config) {}
  std::string name() const override { return "envoy.http.header_validators.envoy_default"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig>();
  }

  ::Envoy::Http::HeaderValidatorFactoryPtr
  createFromProto(const Protobuf::Message& message,
                  ProtobufMessage::ValidationVisitor& validation_visitor) override {
    auto mptr = ::Envoy::Config::Utility::translateAnyToFactoryConfig(
        dynamic_cast<const ProtobufWkt::Any&>(message), validation_visitor, *this);
    const auto& proto_config =
        MessageUtil::downcastAndValidate<const ::envoy::extensions::http::header_validators::
                                             envoy_default::v3::HeaderValidatorConfig&>(
            *mptr, validation_visitor);
    config_ = proto_config;
    auto header_validator =
        std::make_unique<StrictMock<::Envoy::Http::MockHeaderValidatorFactory>>();
    EXPECT_CALL(*header_validator, create(::Envoy::Http::Protocol::Http2, _))
        .WillOnce(InvokeWithoutArgs(
            []() { return std::make_unique<StrictMock<::Envoy::Http::MockHeaderValidator>>(); }));
    return header_validator;
  }

private:
  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig& config_;
};

} // namespace

// Verify plumbing of the header validator factory.
TEST_F(ConfigTest, HeaderValidatorConfig) {
  const std::string yaml_string = R"EOF(
  use_downstream_protocol_config:
    http3_protocol_options: {}
  typed_header_validation_config:
    name: custom_header_validator
    typed_config:
      "@type": type.googleapis.com/test.upstreams.http.CustomHeaderValidator
  )EOF";
  TestHeaderValidatorFactoryConfig factory;
  Registry::InjectFactory<::Envoy::Http::HeaderValidatorFactoryConfig> registration(factory);
  TestUtility::loadFromYamlAndValidate(yaml_string, options_);
#ifdef ENVOY_ENABLE_UHV
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
  NiceMock<::Envoy::Http::MockHeaderValidatorStats> stats;
  EXPECT_NE(nullptr,
            config.header_validator_factory_->create(::Envoy::Http::Protocol::Http2, stats));
#else
  // If UHV is disabled, providing config should result in rejection
  EXPECT_THROW({ ProtocolOptionsConfigImpl config(options_, validation_visitor_); },
               EnvoyException);
#endif
}

TEST_F(ConfigTest, DefaultHeaderValidatorConfig) {
  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      proto_config;
  DefaultHeaderValidatorFactoryConfigOverride factory(proto_config);
  Registry::InjectFactory<::Envoy::Http::HeaderValidatorFactoryConfig> registration(factory);
  NiceMock<::Envoy::Http::MockHeaderValidatorStats> stats;
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
#ifdef ENVOY_ENABLE_UHV
  EXPECT_NE(nullptr,
            config.header_validator_factory_->create(::Envoy::Http::Protocol::Http2, stats));
  EXPECT_FALSE(proto_config.http1_protocol_options().allow_chunked_length());
#else
  // If UHV is disabled, config should be accepted and factory should be nullptr
  EXPECT_EQ(nullptr, config.header_validator_factory_);
#endif
}

TEST_F(ConfigTest, TranslateDownstreamLegacyConfigToDefaultHeaderValidatorConfig) {
  const std::string yaml_string = R"EOF(
  use_downstream_protocol_config:
    http_protocol_options:
      allow_chunked_length: true
  )EOF";

  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml_string, options_);
  DefaultHeaderValidatorFactoryConfigOverride factory(proto_config);
  Registry::InjectFactory<::Envoy::Http::HeaderValidatorFactoryConfig> registration(factory);
  NiceMock<::Envoy::Http::MockHeaderValidatorStats> stats;
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
#ifdef ENVOY_ENABLE_UHV
  EXPECT_NE(nullptr,
            config.header_validator_factory_->create(::Envoy::Http::Protocol::Http2, stats));
  EXPECT_TRUE(proto_config.http1_protocol_options().allow_chunked_length());
#else
  // If UHV is disabled, config should be accepted and factory should be nullptr
  EXPECT_EQ(nullptr, config.header_validator_factory_);
#endif
}

TEST_F(ConfigTest, TranslateAutoLegacyConfigToDefaultHeaderValidatorConfig) {
  const std::string yaml_string = R"EOF(
  auto_config:
    http_protocol_options:
      allow_chunked_length: true
  )EOF";

  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml_string, options_);
  DefaultHeaderValidatorFactoryConfigOverride factory(proto_config);
  Registry::InjectFactory<::Envoy::Http::HeaderValidatorFactoryConfig> registration(factory);
  NiceMock<::Envoy::Http::MockHeaderValidatorStats> stats;
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
#ifdef ENVOY_ENABLE_UHV
  EXPECT_NE(nullptr,
            config.header_validator_factory_->create(::Envoy::Http::Protocol::Http2, stats));
  EXPECT_TRUE(proto_config.http1_protocol_options().allow_chunked_length());
#else
  // If UHV is disabled, config should be accepted and factory should be nullptr
  EXPECT_EQ(nullptr, config.header_validator_factory_);
#endif
}

TEST_F(ConfigTest, TranslateExplicitLegacyConfigToDefaultHeaderValidatorConfig) {
  const std::string yaml_string = R"EOF(
  explicit_http_config:
    http_protocol_options:
      allow_chunked_length: true
  )EOF";

  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
      proto_config;
  TestUtility::loadFromYamlAndValidate(yaml_string, options_);
  DefaultHeaderValidatorFactoryConfigOverride factory(proto_config);
  Registry::InjectFactory<::Envoy::Http::HeaderValidatorFactoryConfig> registration(factory);
  NiceMock<::Envoy::Http::MockHeaderValidatorStats> stats;
  ProtocolOptionsConfigImpl config(options_, validation_visitor_);
#ifdef ENVOY_ENABLE_UHV
  EXPECT_NE(nullptr,
            config.header_validator_factory_->create(::Envoy::Http::Protocol::Http2, stats));
  EXPECT_TRUE(proto_config.http1_protocol_options().allow_chunked_length());
#else
  // If UHV is disabled, config should be accepted and factory should be nullptr
  EXPECT_EQ(nullptr, config.header_validator_factory_);
#endif
}

} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
