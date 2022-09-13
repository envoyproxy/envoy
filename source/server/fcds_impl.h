#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/server/admin.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/init/watcher_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/config/subscription_base.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Server {
class FilterChainManagerImpl;
class FilterChainFactoryBuilder;
/**
 * A class that fetches the filter chain configuration dynamically using the FCDS API and updates them to
 * FCDS config providers.
 */
class FcdsApi
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>,
      Logger::Loggable<Logger::Id::upstream> {

public:
  FcdsApi(
      const envoy::config::listener::v3::Fcds& fcds, 
      Configuration::FactoryContext& parent_context,
      Init::Manager& init_manager,
      FilterChainManagerImpl& filter_chain_manager,
      FilterChainFactoryBuilder* fc_builder);
  
  ~FcdsApi();
  std::string versionInfo() const { return system_version_info_; }

  bool validateUpdateSize(int num_resources);
  const Init::Target& initTarget() { return parent_init_target_; }
  void initialize();
private:
  const std::string fcds_name_;
  Envoy::Config::SubscriptionPtr fcds_subscription_;
  std::string system_version_info_;

  // Init target used to notify the parent init manager that the subscription [and its sub resource]
  // is ready.
  Init::SharedTargetImpl parent_init_target_;
  // Init watcher on FCDS ready event. This watcher marks parent_init_target_ ready.
  Init::WatcherImpl local_init_watcher_;
  // Target which starts the FCDS subscription.
  Init::TargetImpl local_init_target_;
  Init::ManagerImpl local_init_manager_;
  Init::Manager& init_manager_;

  Upstream::ClusterManager&  cluster_manager_;
  Stats::ScopePtr scope_;
  Common::CallbackManager<> update_callback_manager_;

  FilterChainManagerImpl& filter_chain_manager_;
  FilterChainFactoryBuilder* fc_builder_;
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override;

  void onConfigUpdate(const std::vector<Envoy::Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;

  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addUpdateCallback(std::function<void()> callback)
  {  
       return update_callback_manager_.add(callback);
  }
};

using FcdsApiPtr = std::shared_ptr<FcdsApi>;

} // namespace Server
} // namespace Envoy


