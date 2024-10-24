#include "source/common/config/xds_manager_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

using ::Envoy::StatusHelpers::StatusCodeIs;
using testing::HasSubstr;
using testing::Return;

class XdsManagerImplTest : public testing::Test {
public:
  XdsManagerImplTest() : xds_manager_impl_(cm_, validation_context_) {
    ON_CALL(validation_context_, staticValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  XdsManagerImpl xds_manager_impl_;
};

// Validates that setAdsConfigSource invokes the correct method in the cm_.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterSuccess) {
  envoy::config::core::v3::ApiConfigSource config_source;
  config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_CALL(cm_, replaceAds(ProtoEq(config_source))).WillOnce(Return(absl::OkStatus()));
  absl::Status res = xds_manager_impl_.setAdsConfigSource(config_source);
  EXPECT_TRUE(res.ok());
}

// Validates that setAdsConfigSource invokes the correct method in the cm_,
// and fails if needed.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterFailure) {
  envoy::config::core::v3::ApiConfigSource config_source;
  config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_CALL(cm_, replaceAds(ProtoEq(config_source)))
      .WillOnce(Return(absl::InternalError("error")));
  absl::Status res = xds_manager_impl_.setAdsConfigSource(config_source);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_EQ(res.message(), "error");
}

// Validates that setAdsConfigSource validation failure is detected.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterInvalidConfig) {
  envoy::config::core::v3::ApiConfigSource config_source;
  config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  // Add an empty gRPC service (without EnvoyGrpc/GoogleGrpc) which should be
  // invalid.
  config_source.add_grpc_services();
  absl::Status res = xds_manager_impl_.setAdsConfigSource(config_source);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_THAT(res.message(), HasSubstr("Proto constraint validation failed"));
}

} // namespace
} // namespace Config
} // namespace Envoy
