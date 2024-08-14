#include "source/extensions/filters/http/proto_message_logging/extractor.h"
#include "source/extensions/filters/http/proto_message_logging/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_logging/filter_config.h"

#include "test/mocks/http/mocks.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

namespace {

using ::envoy::extensions::filters::http::proto_message_logging::v3::ProtoMessageLoggingConfig;
using ::Envoy::Http::TestRequestTrailerMapImpl;

class FilterConfigTestBase : public ::testing::Test {
protected:
  FilterConfigTestBase() : api_(Api::createApiForTest()) {}

  void parseConfigProto(absl::string_view str = R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "parent" value: LOG }
        request_logging_by_field: { key: "key.name" value: LOG }
        response_logging_by_field: { key: "name" value: LOG }
      }
    })pb") {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(str, &proto_config_));
  }

  Api::ApiPtr api_;
  ProtoMessageLoggingConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;
};

using FilterConfigTestOk = FilterConfigTestBase;

TEST_F(FilterConfigTestOk, DescriptorInline) {
  parseConfigProto();
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, LogRedact) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "parent" value: LOG_REDACT }
        request_logging_by_field: { key: "key.name" value: LOG_REDACT }
        response_logging_by_field: { key: "name" value: LOG_REDACT }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, LogDirectiveUnspecified) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "parent" value: LogDirective_UNSPECIFIED }
        request_logging_by_field: { key: "key.name" value: LogDirective_UNSPECIFIED }
        response_logging_by_field: { key: "name" value: LogDirective_UNSPECIFIED }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, LogModeUnspecified) {
  parseConfigProto(R"pb(
    mode: LogMode_UNSPECIFIED
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "parent" value: LOG }
        request_logging_by_field: { key: "key.name" value: LOG }
        response_logging_by_field: { key: "name" value: LOG }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, DescriptorFromFile) {
  parseConfigProto();
  *proto_config_.mutable_data_source()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, AllSupportedTypes) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "supported_types.string" value: LOG }
        request_logging_by_field: { key: "supported_types.uint32" value: LOG }
        request_logging_by_field: { key: "supported_types.uint64" value: LOG }
        request_logging_by_field: { key: "supported_types.int32" value: LOG }
        request_logging_by_field: { key: "supported_types.int64" value: LOG }
        request_logging_by_field: { key: "supported_types.sint32" value: LOG }
        request_logging_by_field: { key: "supported_types.sint64" value: LOG }
        request_logging_by_field: { key: "supported_types.fixed32" value: LOG }
        request_logging_by_field: { key: "supported_types.fixed64" value: LOG }
        request_logging_by_field: { key: "supported_types.float" value: LOG }
        request_logging_by_field: { key: "supported_types.double" value: LOG }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTestOk, RepeatedField) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: {
          key: "repeated_supported_types.string"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.uint32"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.uint64"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.int32"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.int64"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.sint32"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.sint64"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.fixed32"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.fixed64"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.float"
          value: LOG
        }
        request_logging_by_field: {
          key: "repeated_supported_types.double"
          value: LOG
        }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->findExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->findExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

using FilterConfigTestException = FilterConfigTestBase;
TEST_F(FilterConfigTestException, ErrorParsingDescriptorInline) {
  parseConfigProto();
  *proto_config_.mutable_data_source()->mutable_inline_bytes() = "123";
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException, testing::HasSubstr("unable to parse proto descriptor from inline bytes:"));
}

TEST_F(FilterConfigTestException, ErrorParsingDescriptorFromFile) {
  parseConfigProto();
  *proto_config_.mutable_data_source()->mutable_filename() =
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem");
  EXPECT_THAT_THROWS_MESSAGE(std::make_unique<FilterConfig>(
                                 proto_config_, std::make_unique<ExtractorFactoryImpl>(), *api_),
                             EnvoyException,
                             testing::HasSubstr("unable to parse proto descriptor from file"));
}

TEST_F(FilterConfigTestException, UnsupportedDescriptorSourceType) {
  parseConfigProto();
  *proto_config_.mutable_data_source()->mutable_inline_string() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr("unsupported DataSource case `3` for configuring `descriptor_set`"));
}

TEST_F(FilterConfigTestException, GrpcMethodNotFoundInProtoDescriptor) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "not-found-in-proto-descriptor"
      value: { request_logging_by_field: { key: "parent" value: LOG } }
    }
  )pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();

  EXPECT_THAT_THROWS_MESSAGE(std::make_unique<FilterConfig>(
                                 proto_config_, std::make_unique<ExtractorFactoryImpl>(), *api_),
                             EnvoyException,
                             testing::HasSubstr("couldn't find the gRPC method "
                                                "`not-found-in-proto-descriptor` defined in "
                                                "the proto descriptor"));
}

TEST_F(FilterConfigTestException, UndefinedPathInRequest) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: { request_logging_by_field: { key: "undefined-path" value: LOG } }
    }
  )pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: Invalid fieldPath (undefined-path): no 'undefined-path' field in 'type.googleapis.com/apikeys.CreateApiKeyRequest' message)"));
}

TEST_F(FilterConfigTestException, UndefinedPathInResponse) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "parent" value: LOG }
        response_logging_by_field: { key: "undefined-path" value: LOG }
      }
    }
  )pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: Invalid fieldPath (undefined-path): no 'undefined-path' field in 'type.googleapis.com/apikeys.ApiKey' message)"));
}

TEST_F(FilterConfigTestException, UnsupportedTypeBool) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: { key: "unsupported_types.bool" value: LOG }
      }
    }
  )pb");
  *proto_config_.mutable_data_source()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: leaf node 'bool' must be numerical/string)"));
}

TEST_F(FilterConfigTestException, UnsupportedTypeMessage) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    logging_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_logging_by_field: {
          key: "unsupported_types.message"
          value: LOG
        }
      }
    }
  )pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: leaf node 'message' must be numerical/string)"));
}

} // namespace

} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
