#include "source/extensions/filters/http/grpc_field_extraction/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

class GrpcFieldExtractionFilterFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    api_ = Api::createApiForTest();
    // Load the descriptor file content into the config
    auto descriptor_bytes =
        api_->fileSystem()
            .fileReadToEnd(TestEnvironment::runfilesPath("test/proto/apikeys.descriptor"))
            .value();
    *config_.mutable_descriptor_set()->mutable_inline_bytes() = descriptor_bytes;
  }

  Api::ApiPtr api_;
  envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig config_;
};

TEST_F(GrpcFieldExtractionFilterFactoryTest, CreateFilterFactoryFromProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterFactoryCreator factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config_, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST_F(GrpcFieldExtractionFilterFactoryTest, CreateFilterFactoryFromProtoWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FilterFactoryCreator factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config_, "stats", context);
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
