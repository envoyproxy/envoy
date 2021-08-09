#include "source/extensions/filters/network/thrift_proxy/router/route_config_update_receiver_impl.h"

#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

bool RouteConfigUpdateReceiverImpl::onRdsUpdate(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& rc,
    const std::string& version_info) {
  const uint64_t new_hash = MessageUtil::hash(rc);
  if (new_hash == last_config_hash_) {
    return false;
  }
  route_config_proto_ =
      std::make_unique<envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(
          rc);
  last_config_hash_ = new_hash;
  config_ = std::make_shared<ConfigImpl>(*route_config_proto_);
  last_config_version_ = version_info;
  last_updated_ = time_source_.systemTime();
  config_info_.emplace(RouteConfigProvider::ConfigInfo{*route_config_proto_, last_config_version_});
  return true;
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
