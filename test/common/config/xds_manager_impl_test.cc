#include "source/common/config/xds_manager_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
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
  XdsManagerImplTest()
      : xds_manager_impl_(dispatcher_, api_, local_info_, validation_context_, server_) {
    ON_CALL(validation_context_, staticValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
  }

  void initialize() {
    const envoy::config::bootstrap::v3::Bootstrap bootstrap;
    ASSERT_OK(xds_manager_impl_.initialize(bootstrap, &cm_));
  }

  NiceMock<Server::MockInstance> server_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Api::MockApi> api_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  XdsManagerImpl xds_manager_impl_;
};

// Validates that a call to shutdown succeeds.
TEST_F(XdsManagerImplTest, ShutdownSuccessful) {
  initialize();
  xds_manager_impl_.shutdown();
}

// Validates that setAdsConfigSource invokes the correct method in the
// cluster-manager.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterSuccess) {
  initialize();
  envoy::config::core::v3::ApiConfigSource config_source;
  config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_CALL(cm_, replaceAdsMux(ProtoEq(config_source))).WillOnce(Return(absl::OkStatus()));
  absl::Status res = xds_manager_impl_.setAdsConfigSource(config_source);
  EXPECT_TRUE(res.ok());
}

// Validates that setAdsConfigSource invokes the correct method in the
// cluster-manager, and fails if needed.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterFailure) {
  initialize();
  envoy::config::core::v3::ApiConfigSource config_source;
  config_source.set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_CALL(cm_, replaceAdsMux(ProtoEq(config_source)))
      .WillOnce(Return(absl::InternalError("error")));
  absl::Status res = xds_manager_impl_.setAdsConfigSource(config_source);
  EXPECT_THAT(res, StatusCodeIs(absl::StatusCode::kInternal));
  EXPECT_EQ(res.message(), "error");
}

// Validates that setAdsConfigSource validation failure is detected.
TEST_F(XdsManagerImplTest, AdsConfigSourceSetterInvalidConfig) {
  initialize();
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
