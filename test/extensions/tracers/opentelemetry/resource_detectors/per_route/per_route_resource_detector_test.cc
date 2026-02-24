#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/per_route_resource_detector.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/per_route/resource_typed_metadata.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

using ::envoy::extensions::tracers::opentelemetry::resource_detectors::v3::PerRouteResourceMetadata;

TEST(PerRouteResourceDetectorTest, NullRoute) {
  PerRouteResourceDetector detector;
  testing::NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  ON_CALL(mock_stream_info, route()).WillByDefault(testing::Return(nullptr));
  EXPECT_EQ(detector.detect(mock_stream_info), nullptr);
}

TEST(PerRouteResourceDetectorTest, NullTypedMetadata) {
  PerRouteResourceDetector detector;
  testing::NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  auto route = std::make_shared<testing::NiceMock<Router::MockRoute>>();
  ON_CALL(mock_stream_info, route()).WillByDefault(testing::Return(route));
  EXPECT_EQ(detector.detect(mock_stream_info), nullptr);
}

TEST(PerRouteResourceDetectorTest, ValidTypedMetadata) {
  PerRouteResourceDetector detector;
  testing::NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
  auto route = std::make_shared<testing::NiceMock<Router::MockRoute>>();
  ON_CALL(mock_stream_info, route()).WillByDefault(testing::Return(route));
  auto proto_cfg = PerRouteResourceMetadata();
  proto_cfg.set_schema_url("schema_url");
  Protobuf::Any any;
  any.PackFrom(proto_cfg);
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_typed_filter_metadata())[ResourceTypedRouteMetadataFactory::kName] = any;

  Config::TypedMetadataImpl<Router::HttpRouteTypedMetadataFactory> typed_metadata(metadata);
  ON_CALL(*route, typedMetadata()).WillByDefault(testing::ReturnRef(typed_metadata));
  EXPECT_NE(detector.detect(mock_stream_info), nullptr);
  EXPECT_EQ(detector.detect(mock_stream_info)->schema_url_, "schema_url");
}

TEST(PerRouteResourceDetectorTest, InvalidStructInput) {
  ResourceTypedRouteMetadataFactory factory;
  EXPECT_THROW(factory.parse(Protobuf::Struct()), EnvoyException);
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
