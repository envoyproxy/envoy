#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/grpc_json_transcoder/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {
namespace {

TEST(GrpcJsonTranscoderFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      GrpcJsonTranscoderFilterConfig()
          .createFilterFactoryFromProto(
              envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder(),
              "stats", context)
          .value(),
      ProtoValidationException);
}

TEST(GrpcJsonTranscoderFilterConfigTest, ValidateFailWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW(GrpcJsonTranscoderFilterConfig().createFilterFactoryFromProtoWithServerContext(
                   envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder(),
                   "stats", context),
               ProtoValidationException);
}

class GrpcJsonTranscoderFilterFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    api_ = Api::createApiForTest();
    // Load the descriptor file content into the config
    auto descriptor_bytes =
        api_->fileSystem()
            .fileReadToEnd(TestEnvironment::runfilesPath("test/proto/bookstore.descriptor"))
            .value();
    config_.set_proto_descriptor_bin(descriptor_bytes);
    config_.add_services("bookstore.Bookstore");
  }

  Api::ApiPtr api_;
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder config_;
};

TEST_F(GrpcJsonTranscoderFilterFactoryTest, CreateFilterFactoryFromProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcJsonTranscoderFilterConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config_, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST_F(GrpcJsonTranscoderFilterFactoryTest, CreateFilterFactoryFromProtoWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  GrpcJsonTranscoderFilterConfig factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config_, "stats", context);
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
