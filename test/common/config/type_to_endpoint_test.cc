#include "envoy/api/v2/rds.pb.h"
#include "envoy/service/route/v3/rds.pb.h"

#include "common/config/type_to_endpoint.h"

#include "gtest/gtest.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {
namespace {

// Verify type-to-endpoint methods with RDS as an exemplar.
TEST(TypeToEndpoint, All) {
  // The dummy messages are included for link purposes only.
  envoy::api::v2::RdsDummy _v2_rds_dummy;
  envoy::service::route::v3::RdsDummy _v3_rds_dummy;

  // Delta gRPC endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::V3)
                .full_name());

  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                            envoy::config::core::v3::ApiVersion::V3)
                .full_name());

  // SotW gRPC endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::V3)
                .full_name());

  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                           envoy::config::core::v3::ApiVersion::V3)
                .full_name());

  // REST endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::V3)
                .full_name());

  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::AUTO)
                .full_name());
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::V2)
                .full_name());
  EXPECT_EQ("envoy.service.route.v3.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
                       envoy::config::core::v3::ApiVersion::V3)
                .full_name());
}

} // namespace
} // namespace Config
} // namespace Envoy
