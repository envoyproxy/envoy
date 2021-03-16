#include "extensions/filters/network/mysql_proxy/route_impl.h"

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

#include "extensions/filters/network/mysql_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

RouteImpl::RouteImpl(ConnectionPool::ClientPoolSharedPtr pool) : pool_(pool) {}

RouterImpl::RouterImpl(absl::flat_hash_map<std::string, RouteSharedPtr>&& router)
    : routes_(std::move(router)) {}

RouteSharedPtr RouterImpl::upstreamPool(const std::string& db) {
  if (routes_.find(db) != routes_.end()) {
    return routes_[db];
  }
  return nullptr;
}

RouteFactoryImpl RouteFactoryImpl::instance_;

RouteSharedPtr RouteFactoryImpl::create(
    ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::ConnectionPoolSettings&
        setting,
    DecoderFactory& decoder_factory, ConnectionPool::InstanceFactory& factory,
    const std::string& auth_username, const std::string& auth_password) {
  return std::make_shared<RouteImpl>(
      factory.create(tls, cm, route, setting, decoder_factory, auth_username, auth_password));
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
