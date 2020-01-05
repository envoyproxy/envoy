#include "envoy/api/v2/rds.pb.h"
#include "envoy/service/route/v3alpha/rds.pb.h"

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
  envoy::service::route::v3alpha::RdsDummy _v3_rds_dummy;
  // Delta gRPC endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration").full_name());
  EXPECT_EQ("envoy.service.route.v3alpha.RouteDiscoveryService.DeltaRoutes",
            deltaGrpcMethod("type.googleapis.com/envoy.config.route.v3alpha.RouteConfiguration")
                .full_name());
  // SoTW gRPC endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration").full_name());
  EXPECT_EQ("envoy.service.route.v3alpha.RouteDiscoveryService.StreamRoutes",
            sotwGrpcMethod("type.googleapis.com/envoy.config.route.v3alpha.RouteConfiguration")
                .full_name());
  // REST endpoints.
  EXPECT_EQ("envoy.api.v2.RouteDiscoveryService.FetchRoutes",
            restMethod("type.googleapis.com/envoy.api.v2.RouteConfiguration").full_name());
  EXPECT_EQ(
      "envoy.service.route.v3alpha.RouteDiscoveryService.FetchRoutes",
      restMethod("type.googleapis.com/envoy.config.route.v3alpha.RouteConfiguration").full_name());
}

} // namespace
} // namespace Config
} // namespace Envoy
