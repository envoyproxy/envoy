#pragma once

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/upstream/lazy_loader.h"
#include "envoy/event/dispatcher.h"
#include "envoy/config/subscription.h"

#include "common/config/grpc_mux_impl.h"
#include "common/upstream/cluster_manager_impl.h"

namespace Envoy {
namespace Upstream {

class LazyLoaderImpl : public LazyLoader, Config::SubscriptionCallbacks<envoy::api::v2::Cluster>, Logger::Loggable<Logger::Id::upstream> {
public:
  LazyLoaderImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                 const LocalInfo::LocalInfo& local_info,
                 Event::Dispatcher& primary_dispatcher,
                 Runtime::RandomGenerator& random,
                 ClusterManagerImpl& cluster_manager,
                 Stats::Store& statsconst);

  void loadCluster(const std::string& cluster) override;

  // From Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }

private:
  Optional<envoy::api::v2::core::ConfigSource> eds_config_;
  Config::GrpcMuxPtr ads_mux_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::Cluster>> subscription_;
  Event::Dispatcher& primary_dispatcher_;
  ClusterManagerImpl& cluster_manager_;

};

} // namespace Upstream
} // namespace Envoy
