#include "common/upstream/lazy_loader_impl.h"

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/upstream/lazy_loader.h"
#include "envoy/event/dispatcher.h"

#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/config/grpc_mux_impl.h"


namespace Envoy {
namespace Upstream {


LazyLoaderImpl::LazyLoaderImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                               const LocalInfo::LocalInfo& local_info,
                               Event::Dispatcher& primary_dispatcher,
                               Runtime::RandomGenerator& random,
                               ClusterManagerImpl& cm,
                               Stats::Store& stats) :
                                 primary_dispatcher_(primary_dispatcher),
                                 cluster_manager_(cm) {
  ASSERT(bootstrap.dynamic_resources().has_cds_config());
  if (bootstrap.dynamic_resources().has_ads_config()) {
    // Create an ADS stream for the lazy loader. We can not reuse the cluster manager ADS stream.
    ads_mux_.reset(new Config::GrpcMuxImpl(
        bootstrap.node(),
        Config::Utility::factoryForApiConfigSource(
            cluster_manager_.grpcAsyncClientManager(), bootstrap.dynamic_resources().ads_config(), stats)
            ->create(),
        primary_dispatcher,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources")));
  } else {
    ads_mux_.reset(&cluster_manager_.adsMux());
  }

  if (bootstrap.dynamic_resources().deprecated_v1().has_sds_config()) {
    eds_config_.value(bootstrap.dynamic_resources().deprecated_v1().sds_config());
  }

  subscription_ =
      Config::SubscriptionFactory::cdsSubscriptionFromConfigSource(
          bootstrap.dynamic_resources().cds_config(), eds_config_, local_info, primary_dispatcher,
          cluster_manager_, random, stats, *ads_mux_.get());

  subscription_->start({}, *this);
};

void LazyLoaderImpl::loadCluster(const std::string& cluster) {
  subscription_->updateResources({cluster});
}

void LazyLoaderImpl::onConfigUpdate(const ResourceVector& resources) {
  primary_dispatcher_.post([this, resources]() -> void {
    for (auto r : resources) {
      cluster_manager_.addOrUpdatePrimaryCluster(r, true);
    }
  });
}
void LazyLoaderImpl::onConfigUpdateFailed(const EnvoyException* e) {
  ENVOY_LOG(warn, "gRPC update failed for lazy loader: {}", e->what());
}

} // namespace Upstream
} // namespace Envoy

