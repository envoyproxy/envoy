#include "test/mocks/config/mocks.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"

namespace Envoy {
namespace Config {

MockGrpcMuxWatch::MockGrpcMuxWatch() {}
MockGrpcMuxWatch::~MockGrpcMuxWatch() { cancel(); }

MockGrpcMux::MockGrpcMux() {}
MockGrpcMux::~MockGrpcMux() {}

GrpcMuxWatchPtr MockGrpcMux::subscribe(const std::string& type_url,
                                       const std::vector<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  return GrpcMuxWatchPtr(subscribe_(type_url, resources, callbacks));
}

MockGrpcMuxCallbacks::MockGrpcMuxCallbacks() {
  ON_CALL(*this, resourceName(testing::_))
      .WillByDefault(testing::Invoke([](const ProtobufWkt::Any& resource) -> std::string {
        if (resource.type_url() == Config::TypeUrl::get().Listener) {
          return MessageUtil::anyConvert<envoy::api::v2::Listener>(resource).name();
        }
        if (resource.type_url() == Config::TypeUrl::get().RouteConfiguration) {
          return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
        }
        if (resource.type_url() == Config::TypeUrl::get().Cluster) {
          return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
        }
        if (resource.type_url() == Config::TypeUrl::get().ClusterLoadAssignment) {
          return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource)
              .cluster_name();
        }
        throw EnvoyException(
            fmt::format("Unknown type URL {} in DiscoveryResponse", resource.type_url()));
      }));
}

MockGrpcMuxCallbacks::~MockGrpcMuxCallbacks() {}

} // namespace Config
} // namespace Envoy
