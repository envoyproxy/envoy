
#pragma once
#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "extensions/filters/network/mysql_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class RouteImpl : public Route {
public:
  RouteImpl(ConnPool::ConnectionPoolManagerSharedPtr pool);
  ConnPool::ConnectionPoolManager& upstream() override { return *pool_; }

private:
  ConnPool::ConnectionPoolManagerSharedPtr pool_;
};

class RouterImpl : public Router {
public:
  RouterImpl(absl::flat_hash_map<std::string, RouteSharedPtr>&& router);
  RouteSharedPtr upstreamPool(const std::string& db) override;

private:
  absl::flat_hash_map<std::string, RouteSharedPtr> routes_;
};

class RouteFactoryImpl : public RouteFactory {
public:
  RouteSharedPtr
  create(Upstream::ClusterManager* cm, ThreadLocal::SlotAllocator& tls, Api::Api& api,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         DecoderFactory& decoder_factory, ConnPool::ConnectionPoolManagerFactory& factory) override;
  static RouteFactoryImpl instance;
};

} // namespace MySQLProxy

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
