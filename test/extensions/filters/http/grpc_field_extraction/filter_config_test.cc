#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter_config.h"

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
namespace GrpcFieldExtraction {
namespace {
using ::apikeys::CreateApiKeyRequest;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions;
using ::envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig;
using ::Envoy::Http::MockStreamDecoderFilterCallbacks;
using ::Envoy::Http::MockStreamEncoderFilterCallbacks;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Server::Configuration::FactoryContext;

class FilterConfigTest : public ::testing::Test {
protected:
  FilterConfigTest() : api_(Api::createApiForTest()) {}

  void SetUp() override {
    ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
    request_field_extractions: {
      key: "key.name"
      value: {
      }
    }
  }
}
    )pb",
                                                      &proto_config_));
  }

  Api::ApiPtr api_;
  GrpcFieldExtractionConfig proto_config_;
  std::unique_ptr<FilterConfig> filter_config_;
};

using FilterConfigTestSuccess = FilterConfigTest;
TEST_F(FilterConfigTest, DescriptorInline) {
  *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() =
      api_->fileSystem().fileReadToEnd(
          TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->FindExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->FindExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

TEST_F(FilterConfigTest, DescriptorFromFile) {
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");
  filter_config_ = std::make_unique<FilterConfig>(proto_config_,
                                                  std::make_unique<ExtractorFactoryImpl>(), *api_);
  EXPECT_EQ(filter_config_->FindExtractor("undefined"), nullptr);
  EXPECT_NE(filter_config_->FindExtractor("apikeys.ApiKeys.CreateApiKey"), nullptr);
}

using FilterConfigTestError = FilterConfigTest;
TEST_F(FilterConfigTestError, ErrorParsingDescriptorInline) {
  *proto_config_.mutable_descriptor_set()->mutable_inline_bytes() = "123";
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException, testing::HasSubstr("Unable to parse proto descriptor from inline bytes: "));
}

TEST_F(FilterConfigTestError, ErrorParsingDescriptorFromFile) {
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("wrong-file-path");
  EXPECT_THAT_THROWS_MESSAGE(std::make_unique<FilterConfig>(
                                 proto_config_, std::make_unique<ExtractorFactoryImpl>(), *api_),
                             EnvoyException, testing::HasSubstr("Invalid path: "));
}

TEST_F(FilterConfigTestError, UnsupportedDescriptorSourceTyep) {
  *proto_config_.mutable_descriptor_set()->mutable_inline_string() =
      api_->fileSystem().fileReadToEnd(
          TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"));
  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr("Unsupported DataSource case `3` for configuring `descriptor_set`"));
}

TEST_F(FilterConfigTestError, GrpcMethodNotFoundInProtoDescriptor) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
extractions_by_method: {
  key: "not-found-in-proto-descriptor"
  value: {
    request_field_extractions: {
      key: "parent"
      value: {
      }
    }
  }
}
    )pb",
                                                    &proto_config_));
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr("couldn't find the gRPC method `not-found-in-proto-descriptor` defined in "
                         "the proto descriptor"));
}

TEST_F(FilterConfigTestError, UndefinedPath) {
  ASSERT_TRUE(Protobuf::TextFormat::ParseFromString(R"pb(
extractions_by_method: {
  key: "apikeys.ApiKeys.CreateApiKey"
  value: {
    request_field_extractions: {
      key: "undefined-path"
      value: {
      }
    }
  }
}
    )pb",
                                                    &proto_config_));
  *proto_config_.mutable_descriptor_set()->mutable_filename() =
      TestEnvironment::runfilesPath("test/proto/apikeys.descriptor");

  EXPECT_THAT_THROWS_MESSAGE(
      std::make_unique<FilterConfig>(proto_config_, std::make_unique<ExtractorFactoryImpl>(),
                                     *api_),
      EnvoyException,
      testing::HasSubstr(
          R"(couldn't init extractor for method `apikeys.ApiKeys.CreateApiKey`: Invalid fieldPath (undefined-path): no 'undefined-path' field in 'type.googleapis.com/apikeys.CreateApiKeyRequest' message)"));
}

} // namespace

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
