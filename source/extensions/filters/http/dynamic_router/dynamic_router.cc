#include "extensions/filters/http/dynamic_router/dynamic_router.h"

#include <arpa/inet.h>

#include "common/common/assert.h"
#include "common/config/cds_json.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicRouter {
// Implements StreamDecoderFilter.

Http::FilterHeadersStatus DynamicRouter::decodeHeaders(Http::HeaderMap&, bool) {
  std::cout<<"decode Header..."<<"\n";
  cm_.clusters();
  std::string cluster_json = fmt::sprintf(R"EOF(
  {
    "name": "%s",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://127.0.0.1:11001"}]
  }
  )EOF",
                      "test_dynamic");
 envoy::api::v2::Cluster cluster;
  auto json_object_ptr = Json::Factory::loadFromString(cluster_json);
  Config::CdsJson::translateCluster(*json_object_ptr,
                                    absl::optional<envoy::api::v2::core::ConfigSource>(), cluster);
  dispatcher_.post([cluster,this]()->void {
  bool cluster_output =   cm_.addOrUpdateCluster(cluster,"1");   
  std::cout<<"Cluster Added..."<<cluster_output<<"\n";  
  });
                          
  return Http::FilterHeadersStatus::Continue;
}

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
