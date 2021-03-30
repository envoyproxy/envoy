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

RouteImpl::RouteImpl(ConnPool::ConnectionPoolManagerSharedPtr pool) : pool_(pool) {}

RouterImpl::RouterImpl(absl::flat_hash_map<std::string, RouteSharedPtr>&& router)
    : routes_(std::move(router)) {}

RouteSharedPtr RouterImpl::upstreamPool(const std::string& db) {
  if (routes_.find(db) != routes_.end()) {
    return routes_[db];
  }
  return nullptr;
}

RouteFactoryImpl RouteFactoryImpl::instance;

RouteSharedPtr RouteFactoryImpl::create(
    Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls, Api::Api& api,
    const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
    DecoderFactory& decoder_factory, ConnPool::ConnectionPoolManagerFactory& factory) {
  return std::make_shared<RouteImpl>(factory.create(cm, tls, api, route, decoder_factory));
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
