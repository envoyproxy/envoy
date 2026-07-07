#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/extensions/formatter/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/extensions/formatter/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {
namespace {

using ::Envoy::Formatter::CommandParserFactory;
using ::Envoy::Formatter::SubstitutionFormatStringUtils;

// Builds a proto config that loads the named module with the given in-module formatter name.
envoy::extensions::formatter::dynamic_modules::v3::DynamicModuleFormatter
protoConfig(absl::string_view module_name, absl::string_view formatter_name) {
  const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
formatter_name: {}
)EOF",
                                       module_name, formatter_name);
  envoy::extensions::formatter::dynamic_modules::v3::DynamicModuleFormatter proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return proto_config;
}

// Tests for the factory using minimal C modules to drive symbol resolution and config lifecycle.
class DynamicModuleFormatterFactoryTest : public testing::Test {
public:
  DynamicModuleFormatterFactoryTest() {
    const std::string shared_object_dir =
        std::filesystem::path(
            Extensions::DynamicModules::testSharedObjectPath("formatter_no_op", "c"))
            .parent_path()
            .string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  DynamicModuleFormatterFactory factory_;
};

TEST_F(DynamicModuleFormatterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.formatter.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleFormatterFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(
      nullptr,
      dynamic_cast<envoy::extensions::formatter::dynamic_modules::v3::DynamicModuleFormatter*>(
          proto.get()));
}

TEST_F(DynamicModuleFormatterFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<CommandParserFactory>::getFactory(
      "envoy.formatter.dynamic_modules");
  EXPECT_NE(nullptr, factory);
}

TEST_F(DynamicModuleFormatterFactoryTest, ValidConfig) {
  auto proto_config = protoConfig("formatter_no_op", "test_formatter");
  auto parser = factory_.createCommandParserFromProto(proto_config, context_);
  EXPECT_NE(nullptr, parser);
}

TEST_F(DynamicModuleFormatterFactoryTest, InvalidModule) {
  auto proto_config = protoConfig("nonexistent_module", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to load.*");
}

TEST_F(DynamicModuleFormatterFactoryTest, MissingConfigNew) {
  auto proto_config = protoConfig("formatter_missing_config_new", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to create formatter config.*config_new");
}

TEST_F(DynamicModuleFormatterFactoryTest, MissingConfigDestroy) {
  auto proto_config = protoConfig("formatter_missing_config_destroy", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to create formatter config.*config_destroy");
}

TEST_F(DynamicModuleFormatterFactoryTest, MissingParse) {
  auto proto_config = protoConfig("formatter_missing_parse", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to create formatter config.*formatter_parse");
}

TEST_F(DynamicModuleFormatterFactoryTest, MissingProviderDestroy) {
  auto proto_config = protoConfig("formatter_missing_provider_destroy", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to create formatter config.*provider_destroy");
}

TEST_F(DynamicModuleFormatterFactoryTest, MissingFormat) {
  auto proto_config = protoConfig("formatter_missing_format", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to create formatter config.*formatter_format");
}

TEST_F(DynamicModuleFormatterFactoryTest, ConfigNewReturnsNull) {
  auto proto_config = protoConfig("formatter_config_new_fail", "test_formatter");
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException,
                          "Failed to create formatter config.*Failed to initialize");
}

// A StringValue formatter config is passed to the module as raw bytes.
TEST_F(DynamicModuleFormatterFactoryTest, FormatterConfigStringValue) {
  auto proto_config = protoConfig("formatter_no_op", "test_formatter");
  Protobuf::StringValue string_value;
  string_value.set_value("x-request-id");
  std::ignore = proto_config.mutable_formatter_config()->PackFrom(string_value);
  auto parser = factory_.createCommandParserFromProto(proto_config, context_);
  EXPECT_NE(nullptr, parser);
}

// A Struct formatter config is serialized to JSON before it is passed to the module.
TEST_F(DynamicModuleFormatterFactoryTest, FormatterConfigStruct) {
  auto proto_config = protoConfig("formatter_no_op", "test_formatter");
  Protobuf::Struct struct_value;
  (*struct_value.mutable_fields())["key"] = ValueUtil::stringValue("value");
  std::ignore = proto_config.mutable_formatter_config()->PackFrom(struct_value);
  auto parser = factory_.createCommandParserFromProto(proto_config, context_);
  EXPECT_NE(nullptr, parser);
}

// A formatter config that cannot be parsed as its declared type is rejected.
TEST_F(DynamicModuleFormatterFactoryTest, FormatterConfigMalformedRejected) {
  auto proto_config = protoConfig("formatter_no_op", "test_formatter");
  auto* formatter_config = proto_config.mutable_formatter_config();
  formatter_config->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  formatter_config->set_value(std::string("\x0a\x05\x41", 3));
  EXPECT_THROW_WITH_REGEX(factory_.createCommandParserFromProto(proto_config, context_),
                          EnvoyException, "Failed to parse formatter config.*");
}

// Tests that drive the full parse and format path through the reference Rust module. These exercise
// the command parser, every formatter provider, and the formatter callbacks end to end.
class DynamicModuleFormatterTest : public testing::Test {
public:
  DynamicModuleFormatterTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
    formatter_context_.setRequestHeaders(request_headers_)
        .setResponseHeaders(response_headers_)
        .setResponseTrailers(response_trailers_);
  }

  // Parses a single format string with the dynamic module command parser and formats it.
  std::string format(absl::string_view inline_format) {
    const std::string yaml = fmt::format(R"EOF(
text_format_source:
  inline_string: "{}"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF",
                                         inline_format);
    envoy::config::core::v3::SubstitutionFormatString config;
    TestUtility::loadFromYaml(yaml, config);
    auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config, context_);
    EXPECT_TRUE(formatter.status().ok()) << formatter.status().message();
    return (*formatter)->format(formatter_context_, stream_info_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "GET"}, {":path", "/"}, {"x-custom", "custom-value"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"}, {"x-resp", "resp-value"}};
  Http::TestResponseTrailerMapImpl response_trailers_{{"x-trailer", "trailer-value"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  ::Envoy::Formatter::Context formatter_context_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
};

TEST_F(DynamicModuleFormatterTest, RequestHeader) {
  EXPECT_EQ("custom-value", format("%DYNAMIC_MODULE_REQ(x-custom)%"));
}

TEST_F(DynamicModuleFormatterTest, RequestHeaderMissingRendersDefault) {
  EXPECT_EQ("-", format("%DYNAMIC_MODULE_REQ(x-absent)%"));
}

TEST_F(DynamicModuleFormatterTest, RequestHeaderTruncated) {
  EXPECT_EQ("cus", format("%DYNAMIC_MODULE_REQ(x-custom):3%"));
}

TEST_F(DynamicModuleFormatterTest, ResponseHeader) {
  EXPECT_EQ("resp-value", format("%DYNAMIC_MODULE_RESP(x-resp)%"));
}

TEST_F(DynamicModuleFormatterTest, ResponseTrailer) {
  EXPECT_EQ("trailer-value", format("%DYNAMIC_MODULE_TRAILER(x-trailer)%"));
}

TEST_F(DynamicModuleFormatterTest, RequestHeaderValueAtIndex) {
  Http::TestRequestHeaderMapImpl headers{{"x-dup", "first"}, {"x-dup", "second"}};
  ::Envoy::Formatter::Context context;
  context.setRequestHeaders(headers);
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DYNAMIC_MODULE_REQ_AT(x-dup,1)%"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF";
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml, config);
  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config, context_);
  ASSERT_TRUE(formatter.status().ok()) << formatter.status().message();
  EXPECT_EQ("second", (*formatter)->format(context, stream_info_));
}

TEST_F(DynamicModuleFormatterTest, EmptyValueRendersEmpty) {
  EXPECT_EQ("", format("%DYNAMIC_MODULE_EMPTY%"));
  EXPECT_EQ("[]", format("[%DYNAMIC_MODULE_EMPTY%]"));
}

TEST_F(DynamicModuleFormatterTest, RequestId) {
  StreamInfo::StreamIdProviderImpl provider("ffffffff-0012-0110-00ff-0c00400600ff");
  ON_CALL(stream_info_, getStreamIdProvider())
      .WillByDefault(testing::Return(makeOptRef<const StreamInfo::StreamIdProvider>(provider)));
  EXPECT_EQ("ffffffff-0012-0110-00ff-0c00400600ff", format("%DYNAMIC_MODULE_REQUEST_ID%"));
}

TEST_F(DynamicModuleFormatterTest, ResponseCode) {
  stream_info_.response_code_ = 200;
  EXPECT_EQ("200", format("%DYNAMIC_MODULE_RESP_CODE%"));
}

TEST_F(DynamicModuleFormatterTest, Protocol) {
  stream_info_.protocol_ = Http::Protocol::Http11;
  EXPECT_EQ("HTTP/1.1", format("%DYNAMIC_MODULE_PROTOCOL%"));
}

TEST_F(DynamicModuleFormatterTest, MutualTls) {
  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*ssl_info, peerCertificatePresented()).WillByDefault(testing::Return(true));
  stream_info_.downstream_connection_info_provider_->setSslConnection(ssl_info);
  EXPECT_EQ("true", format("%DYNAMIC_MODULE_MTLS%"));
}

TEST_F(DynamicModuleFormatterTest, MutualTlsAbsentRendersDefault) {
  EXPECT_EQ("-", format("%DYNAMIC_MODULE_MTLS%"));
}

TEST_F(DynamicModuleFormatterTest, DynamicMetadata) {
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["key"] = ValueUtil::stringValue("value");
  (*stream_info_.metadata_.mutable_filter_metadata())["my_filter"] = metadata;
  EXPECT_EQ("value", format("%DYNAMIC_MODULE_META(my_filter:key)%"));
}

TEST_F(DynamicModuleFormatterTest, RequestHeaderCount) {
  EXPECT_EQ("3", format("%DYNAMIC_MODULE_HEADER_COUNT%"));
}

TEST_F(DynamicModuleFormatterTest, RequestHeaderKeys) {
  Http::TestRequestHeaderMapImpl headers{{"x-a", "1"}, {"x-b", "2"}};
  ::Envoy::Formatter::Context context;
  context.setRequestHeaders(headers);
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DYNAMIC_MODULE_REQ_KEYS%"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF";
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml, config);
  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config, context_);
  ASSERT_TRUE(formatter.status().ok()) << formatter.status().message();
  EXPECT_EQ("x-a,x-b", (*formatter)->format(context, stream_info_));
}

TEST_F(DynamicModuleFormatterTest, ResponseHeaderCount) {
  EXPECT_EQ("2", format("%DYNAMIC_MODULE_RESP_COUNT%"));
}

TEST_F(DynamicModuleFormatterTest, ResponseHeaderKeys) {
  EXPECT_EQ(":status,x-resp", format("%DYNAMIC_MODULE_RESP_KEYS%"));
}

TEST_F(DynamicModuleFormatterTest, ResponseTrailerKeys) {
  EXPECT_EQ("x-trailer", format("%DYNAMIC_MODULE_TRAILER_KEYS%"));
}

TEST_F(DynamicModuleFormatterTest, AbsentHeaderMapRendersEmpty) {
  ::Envoy::Formatter::Context context;
  context.setRequestHeaders(request_headers_);
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DYNAMIC_MODULE_RESP_COUNT%,%DYNAMIC_MODULE_RESP_KEYS%"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF";
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml, config);
  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config, context_);
  ASSERT_TRUE(formatter.status().ok()) << formatter.status().message();
  EXPECT_EQ("0,", (*formatter)->format(context, stream_info_));
}

TEST_F(DynamicModuleFormatterTest, ProtocolAbsentRendersDefault) {
  EXPECT_EQ("-", format("%DYNAMIC_MODULE_PROTOCOL%"));
}

TEST_F(DynamicModuleFormatterTest, DynamicMetadataAbsentRendersDefault) {
  EXPECT_EQ("-", format("%DYNAMIC_MODULE_META(missing_filter:key)%"));
}

TEST_F(DynamicModuleFormatterTest, LocalReplyBody) {
  formatter_context_.setLocalReplyBody("rejected");
  EXPECT_EQ("rejected", format("%DYNAMIC_MODULE_LOCAL_REPLY%"));
}

TEST_F(DynamicModuleFormatterTest, AccessLogTypeRendersString) {
  using ::Envoy::Formatter::AccessLogType;
  const std::vector<std::pair<AccessLogType, std::string>> cases = {
      {AccessLogType::NotSet, "NotSet"},
      {AccessLogType::TcpUpstreamConnected, "TcpUpstreamConnected"},
      {AccessLogType::TcpPeriodic, "TcpPeriodic"},
      {AccessLogType::TcpConnectionEnd, "TcpConnectionEnd"},
      {AccessLogType::DownstreamStart, "DownstreamStart"},
      {AccessLogType::DownstreamPeriodic, "DownstreamPeriodic"},
      {AccessLogType::DownstreamEnd, "DownstreamEnd"},
      {AccessLogType::UpstreamPoolReady, "UpstreamPoolReady"},
      {AccessLogType::UpstreamPeriodic, "UpstreamPeriodic"},
      {AccessLogType::UpstreamEnd, "UpstreamEnd"},
      {AccessLogType::DownstreamTunnelSuccessfullyEstablished,
       "DownstreamTunnelSuccessfullyEstablished"},
      {AccessLogType::UdpTunnelUpstreamConnected, "UdpTunnelUpstreamConnected"},
      {AccessLogType::UdpPeriodic, "UdpPeriodic"},
      {AccessLogType::UdpSessionEnd, "UdpSessionEnd"},
  };
  for (const auto& [log_type, expected] : cases) {
    SCOPED_TRACE(expected);
    formatter_context_.setAccessLogType(log_type);
    EXPECT_EQ(expected, format("%DYNAMIC_MODULE_LOG_TYPE%"));
  }
}

TEST_F(DynamicModuleFormatterTest, ConstantValue) {
  EXPECT_EQ("constant", format("%DYNAMIC_MODULE_CONST%"));
}

TEST_F(DynamicModuleFormatterTest, MixedFormat) {
  stream_info_.response_code_ = 200;
  EXPECT_EQ("custom-value 200 constant",
            format("%DYNAMIC_MODULE_REQ(x-custom)% %DYNAMIC_MODULE_RESP_CODE% "
                   "%DYNAMIC_MODULE_CONST%"));
}

TEST_F(DynamicModuleFormatterTest, JsonFormatValue) {
  const std::string yaml = R"EOF(
json_format:
  value: "%DYNAMIC_MODULE_CONST%"
  missing: "%DYNAMIC_MODULE_REQ(x-absent)%"
  empty: "%DYNAMIC_MODULE_EMPTY%"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF";
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml, config);
  auto formatter = SubstitutionFormatStringUtils::fromProtoConfig(config, context_);
  ASSERT_TRUE(formatter.status().ok()) << formatter.status().message();

  const std::string expected = R"EOF({
  "value": "constant",
  "missing": null,
  "empty": ""
})EOF";
  EXPECT_TRUE(TestUtility::jsonStringEqual((*formatter)->format(formatter_context_, stream_info_),
                                           expected));
}

TEST_F(DynamicModuleFormatterTest, UnknownCommandRejected) {
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "%DYNAMIC_MODULE_UNKNOWN%"
formatters:
- name: envoy.formatter.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.dynamic_modules.v3.DynamicModuleFormatter
    dynamic_module_config:
      name: formatter_integration_test
      do_not_close: true
    formatter_name: test_formatter
)EOF";
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml, config);
  EXPECT_FALSE(SubstitutionFormatStringUtils::fromProtoConfig(config, context_).status().ok());
}

TEST_F(DynamicModuleFormatterTest, UnknownFormatterNameRejectedAtConfigLoad) {
  DynamicModuleFormatterFactory factory;
  auto proto_config = protoConfig("formatter_integration_test", "unknown_formatter");
  EXPECT_THROW_WITH_REGEX(factory.createCommandParserFromProto(proto_config, context_),
                          EnvoyException,
                          "Failed to create formatter config.*Failed to initialize");
}

} // namespace
} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
