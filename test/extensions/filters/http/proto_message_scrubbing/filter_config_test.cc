#include "source/extensions/filters/http/proto_message_scrubbing/extractor.h"
#include "source/extensions/filters/http/proto_message_scrubbing/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_scrubbing/filter_config.h"

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
namespace ProtoMessageScrubbing {

namespace {

using ::envoy::extensions::filters::http::proto_message_scrubbing::v3::ProtoMessageScrubbingConfig;
using ::Envoy::Http::TestRequestTrailerMapImpl;

class FilterConfigTestBase : public ::testing::Test {
protected:
  FilterConfigTestBase() : api_(Api::createApiForTest()) {}

  void parseConfigProto(absl::string_view str = R"pb(
    mode: FIRST_AND_LAST
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "parent" value: SCRUB }
        request_scrubbing_by_field: { key: "key.name" value: SCRUB }
        response_scrubbing_by_field: { key: "name" value: SCRUB }
      }
    })pb") {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(str, &proto_config_));
  }

  Api::ApiPtr api_;
  ProtoMessageScrubbingConfig proto_config_;
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

TEST_F(FilterConfigTestOk, ScrubRedact) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "parent" value: SCRUB_REDACT }
        request_scrubbing_by_field: { key: "key.name" value: SCRUB_REDACT }
        response_scrubbing_by_field: { key: "name" value: SCRUB_REDACT }
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

TEST_F(FilterConfigTestOk, ScrubDirectiveUnspecified) {
  parseConfigProto(R"pb(
    mode: FIRST_AND_LAST
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "parent" value: ScrubDirective_UNSPECIFIED }
        request_scrubbing_by_field: { key: "key.name" value: ScrubDirective_UNSPECIFIED }
        response_scrubbing_by_field: { key: "name" value: ScrubDirective_UNSPECIFIED }
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

TEST_F(FilterConfigTestOk, ScrubModeUnspecified) {
  parseConfigProto(R"pb(
    mode: ScrubMode_UNSPECIFIED
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "parent" value: SCRUB }
        request_scrubbing_by_field: { key: "key.name" value: SCRUB }
        response_scrubbing_by_field: { key: "name" value: SCRUB }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "supported_types.string" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.uint32" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.uint64" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.int32" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.int64" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.sint32" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.sint64" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.fixed32" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.fixed64" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.float" value: SCRUB }
        request_scrubbing_by_field: { key: "supported_types.double" value: SCRUB }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: {
          key: "repeated_supported_types.string"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.uint32"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.uint64"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.int32"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.int64"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.sint32"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.sint64"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.fixed32"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.fixed64"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.float"
          value: SCRUB
        }
        request_scrubbing_by_field: {
          key: "repeated_supported_types.double"
          value: SCRUB
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
    scrubbing_by_method: {
      key: "not-found-in-proto-descriptor"
      value: { request_scrubbing_by_field: { key: "parent" value: SCRUB } }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: { request_scrubbing_by_field: { key: "undefined-path" value: SCRUB } }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "parent" value: SCRUB }
        response_scrubbing_by_field: { key: "undefined-path" value: SCRUB }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: { key: "unsupported_types.bool" value: SCRUB }
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
    scrubbing_by_method: {
      key: "apikeys.ApiKeys.CreateApiKey"
      value: {
        request_scrubbing_by_field: {
          key: "unsupported_types.message"
          value: SCRUB
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

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
