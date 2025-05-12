#include "source/extensions/filters/http/proto_message_extraction/config.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_extraction/filter_config.h"

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
namespace ProtoMessageExtraction {

namespace {

using ::envoy::extensions::filters::http::proto_message_extraction::v3::
    ProtoMessageExtractionConfig;
using ::Envoy::Http::TestRequestTrailerMapImpl;

class FilterConfigTestBase : public ::testing::Test {
protected:
  FilterConfigTestBase() : api_(Api::createApiForTest()) {}

  void parseConfigProto(absl::string_view str = R"pb(
    mode: FIRST_AND_LAST
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "parent" value: EXTRACT }
        request_extraction_by_field: { key: "key.name" value: EXTRACT }
        response_extraction_by_field: { key: "name" value: EXTRACT }
      }
    })pb") {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(str, &proto_config_));
  }

  Api::ApiPtr api_;
  ProtoMessageExtractionConfig proto_config_;
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

TEST_F(FilterConfigTestOk, ExtractDirectiveUnspecified) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "parent" value: ExtractDirective_UNSPECIFIED }
        request_extraction_by_field: { key: "key.name" value: ExtractDirective_UNSPECIFIED }
        response_extraction_by_field: { key: "name" value: ExtractDirective_UNSPECIFIED }
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

TEST_F(FilterConfigTestOk, ExtractModeUnspecified) {
  parseConfigProto(R"pb(
    mode: ExtractMode_UNSPECIFIED
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "parent" value: EXTRACT }
        request_extraction_by_field: { key: "key.name" value: EXTRACT }
        response_extraction_by_field: { key: "name" value: EXTRACT }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "supported_types.string" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.uint32" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.uint64" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.int32" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.int64" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.sint32" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.sint64" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.fixed32" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.fixed64" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.float" value: EXTRACT }
        request_extraction_by_field: { key: "supported_types.double" value: EXTRACT }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: {
          key: "repeated_supported_types.string"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.uint32"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.uint64"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.int32"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.int64"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.sint32"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.sint64"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.fixed32"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.fixed64"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.float"
          value: EXTRACT
        }
        request_extraction_by_field: {
          key: "repeated_supported_types.double"
          value: EXTRACT
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

TEST_F(FilterConfigTestOk, ExtractRedact) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: {
          key: "repeated_supported_types.string"
          value: EXTRACT_REDACT
        }
        response_extraction_by_field: {
          key: "name"
          value: EXTRACT_REDACT
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
    extraction_by_method: {
      key: "not-found-in-proto-descriptor"
      value: { request_extraction_by_field: { key: "parent" value: EXTRACT } }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: { request_extraction_by_field: { key: "undefined-path" value: EXTRACT } }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "parent" value: EXTRACT }
        response_extraction_by_field: { key: "undefined-path" value: EXTRACT }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "unsupported_types.bool" value: EXTRACT }
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
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: {
          key: "unsupported_types.message"
          value: EXTRACT
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

TEST_F(FilterConfigTestException, RedactNonLeafField) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    extraction_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_extraction_by_field: { key: "key" value: EXTRACT_REDACT }
      }
    })pb");
  *proto_config_.mutable_data_source()->mutable_inline_bytes() =
      api_->fileSystem()
          .fileReadToEnd(Envoy::TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
          .value();

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: leaf node 'key' must be numerical/string or timestamp type)"));
}

TEST(FilterFactoryCreatorTest, Constructor) {
  FilterFactoryCreator factory;
  EXPECT_EQ(factory.name(), "envoy.filters.http.proto_message_extraction");
}

} // namespace

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
