#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/grpc_json_reverse_transcoder/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

} // namespace
} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
