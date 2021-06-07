
#pragma once
#include "envoy/api/api.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/mysql_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class RouteImpl : public Route {
public:
  RouteImpl(Upstream::ClusterManager* cm, const std::string& cluster_name);
  Upstream::ThreadLocalCluster* upstream() override {
    return cm_->getThreadLocalCluster(cluster_name_);
  }
  const std::string& name() override { return cluster_name_; }

private:
  const std::string cluster_name_;
  Upstream::ClusterManager* cm_;
};

class RouterImpl : public Router {
public:
  RouterImpl(RouteSharedPtr catch_all_route,
             absl::flat_hash_map<std::string, RouteSharedPtr>&& router);
  RouteSharedPtr upstreamPool(const std::string& db) override;
  RouteSharedPtr defaultPool() override;

private:
  RouteSharedPtr catch_all_route_;
  absl::flat_hash_map<std::string, RouteSharedPtr> routes_;
};

class RouteFactoryImpl : public RouteFactory {
public:
  RouteSharedPtr create(Upstream::ClusterManager* cm, const std::string& cluster_name) override;
  static RouteFactoryImpl instance;
};

} // namespace MySQLProxy

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
