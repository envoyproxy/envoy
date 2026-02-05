#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/grpc_json_reverse_transcoder/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {
namespace {

TEST(GrpcJsonTranscoderFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(GrpcJsonReverseTranscoderFactory()
                   .createFilterFactoryFromProto(
                       envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
                           GrpcJsonReverseTranscoder(),
                       "stats", context)
                   .value(),
               ProtoValidationException);
}

TEST(GrpcJsonTranscoderFilterConfigTest, ValidateFailWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW(GrpcJsonReverseTranscoderFactory().createFilterFactoryFromProtoWithServerContext(
                   envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
                       GrpcJsonReverseTranscoder(),
                   "stats", context),
               ProtoValidationException);
}

class GrpcJsonReverseTranscoderFilterFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    api_ = Api::createApiForTest();
    // Load the descriptor file content into the config
    auto descriptor_bytes =
        api_->fileSystem()
            .fileReadToEnd(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"))
            .value();
    config_.set_descriptor_binary(descriptor_bytes);
  }

  Api::ApiPtr api_;
  envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::GrpcJsonReverseTranscoder
      config_;
};

TEST_F(GrpcJsonReverseTranscoderFilterFactoryTest, CreateFilterFactoryFromProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcJsonReverseTranscoderFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config_, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST_F(GrpcJsonReverseTranscoderFilterFactoryTest, CreateFilterFactoryFromProtoWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  GrpcJsonReverseTranscoderFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config_, "stats", context);
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
