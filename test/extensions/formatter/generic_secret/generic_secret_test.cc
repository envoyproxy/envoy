#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/extensions/formatter/generic_secret/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class GenericSecretFormatterTest : public ::testing::Test {
public:
  void SetUp() override {
    // Reset the secret manager so it starts empty for each test.
    context_.server_factory_context_.resetSecretManager();
  }

  // Register a static generic secret with the given name and inline_string value.
  void addStaticSecret(const std::string& name, const std::string& value) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(name);
    secret.mutable_generic_secret()->mutable_secret()->set_inline_string(value);
    ASSERT_TRUE(context_.server_factory_context_.secretManager().addStaticSecret(secret).ok());
  }

  envoy::config::core::v3::SubstitutionFormatString config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  StreamInfo::MockStreamInfo stream_info_;
  ::Envoy::Formatter::Context formatter_context_;

  ::Envoy::Formatter::FormatterPtr makeFormatter(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, config_);
    return THROW_OR_RETURN_VALUE(
        ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config_, context_),
        ::Envoy::Formatter::FormatterPtr);
  }
};

// A known static secret name resolves to the configured value.
TEST_F(GenericSecretFormatterTest, StaticSecretResolvesToValue) {
  addStaticSecret("my-token", "s3cret");

  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "prefix-%SECRET(my-token)%-suffix"
formatters:
- name: envoy.formatter.generic_secret
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret
    secret_configs:
      my-token:
        name: my-token
)EOF";

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("prefix-s3cret-suffix", formatter->format(formatter_context_, stream_info_));
}

// An unknown secret name (not in the formatter config) is rejected at parse time.
TEST_F(GenericSecretFormatterTest, UnknownSecretNameThrows) {
  addStaticSecret("my-token", "s3cret");

  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%SECRET(my-token)% %SECRET(unknown)%"
formatters:
- name: envoy.formatter.generic_secret
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret
    secret_configs:
      my-token:
        name: my-token
)EOF";

  EXPECT_THROW_WITH_MESSAGE(
      makeFormatter(yaml), EnvoyException,
      "envoy.formatter.generic_secret: secret 'unknown' is not configured in secret_configs");
}

// Multiple secrets can be configured and resolve independently.
TEST_F(GenericSecretFormatterTest, MultipleSecrets) {
  addStaticSecret("token-a", "alpha");
  addStaticSecret("token-b", "bravo");

  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%SECRET(a)%:%SECRET(b)%"
formatters:
- name: envoy.formatter.generic_secret
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret
    secret_configs:
      a:
        name: token-a
      b:
        name: token-b
)EOF";

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("alpha:bravo", formatter->format(formatter_context_, stream_info_));
}

// If the static secret provider is not found, construction throws.
TEST_F(GenericSecretFormatterTest, StaticSecretNotFoundThrows) {
  // Do not add any static secrets.
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%SECRET(missing)%"
formatters:
- name: envoy.formatter.generic_secret
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.generic_secret.v3.GenericSecret
    secret_configs:
      missing:
        name: no-such-secret
)EOF";

  EXPECT_THROW_WITH_MESSAGE(makeFormatter(yaml), EnvoyException,
                            "envoy.formatter.generic_secret: secret 'no-such-secret' not found in "
                            "static bootstrap resources");
}

// formatValue() returns a Protobuf::Value with the secret string.
TEST_F(GenericSecretFormatterTest, FormatValueReturnsStringValue) {
  addStaticSecret("my-token", "s3cret");

  envoy::extensions::formatter::generic_secret::v3::GenericSecret proto_config;
  (*proto_config.mutable_secret_configs())["my-token"].set_name("my-token");

  GenericSecretFormatterFactory factory;
  auto parser = factory.createCommandParserFromProto(proto_config, context_);

  auto provider = parser->parse("SECRET", "my-token", absl::nullopt);
  ASSERT_NE(nullptr, provider);

  auto value = provider->formatValue(formatter_context_, stream_info_);
  EXPECT_EQ("s3cret", value.string_value());
}

// parse() returns nullptr for commands other than SECRET.
TEST_F(GenericSecretFormatterTest, ParseIgnoresOtherCommands) {
  addStaticSecret("my-token", "s3cret");

  envoy::extensions::formatter::generic_secret::v3::GenericSecret proto_config;
  (*proto_config.mutable_secret_configs())["my-token"].set_name("my-token");

  GenericSecretFormatterFactory factory;
  auto parser = factory.createCommandParserFromProto(proto_config, context_);

  auto provider = parser->parse("NOT_SECRET", "my-token", absl::nullopt);
  EXPECT_EQ(nullptr, provider);
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
