
#pragma once
#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class RouteImpl : public Route {
public:
  RouteImpl(ConnectionPool::ClientPoolSharedPtr pool);
  ConnectionPool::Instance& upstream() override { return *pool_; }

private:
  ConnectionPool::ClientPoolSharedPtr pool_;
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
  create(ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager* cm,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::Route& route,
         const envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy::
             ConnectionPoolSettings& setting,
         DecoderFactory& decoder_factory, ConnectionPool::InstanceFactory& factory,
         const std::string& auth_username, const std::string& auth_password) override;
  static RouteFactoryImpl instance_;
};

} // namespace MySQLProxy

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
