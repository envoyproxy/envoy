#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

// #include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/service/tap/v2alpha/tds.pb.validate.h"

#include "envoy/service/tap/v2alpha/common.pb.h"
#include "envoy/config/common/tap/v2alpha/common.pb.h"

#include "envoy/config/subscription.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/server/admin.h"
#include "envoy/server/filter_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "extensions/common/tap/tap.h"

#include "common/common/logger.h"
#include "common/init/target_impl.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {


/**
 * All RDS stats. @see stats_macros.h
 */
// clang-format off
#define ALL_TDS_STATS(COUNTER)                                                                     \
  COUNTER(config_reload)                                                                           \
  COUNTER(update_empty)

// clang-format on

/**
 * Struct definition for all RDS stats. @see stats_macros.h
 */
struct TdsStats {
  ALL_TDS_STATS(GENERATE_COUNTER_STRUCT)
};

class TdsTapConfigSubscriptionHandle{
public:
  // handle can only be moved.
  TdsTapConfigSubscriptionHandle(const TdsTapConfigSubscriptionHandle&) = delete;
  TdsTapConfigSubscriptionHandle(){}
  // PURE DTor to make this class abstract.
  virtual ~TdsTapConfigSubscriptionHandle() PURE;

};

typedef std::unique_ptr<TdsTapConfigSubscriptionHandle> TdsTapConfigSubscriptionHandlePtr;

class TapConfigProviderManager {
public:
  virtual ~TapConfigProviderManager(){}

  // RouteConfigProviderManager
  virtual TdsTapConfigSubscriptionHandlePtr subscribeTap(
      const envoy::config::common::tap::v2alpha::CommonExtensionConfig_TDSConfig& tds,
      ExtensionConfig& ptr,

      const std::string& stat_prefix,
      Stats::Scope& stats,
      Upstream::ClusterManager& cluster_Manager,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher&  main_thread_dispatcher,
      Envoy::Runtime::RandomGenerator& random,
      Api::Api& api

      ) PURE;

};

////////////////////////// impl

class TapConfigProviderManagerImpl;
/**
 * A class that fetches the route configuration dynamically using the RDS API and updates them to
 * RDS config providers.
 */
class TdsTapConfigSubscription : public Envoy::Config::SubscriptionCallbacks,
                                   Logger::Loggable<Logger::Id::router> {
public:
  ~TdsTapConfigSubscription() override;

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::service::tap::v2alpha::TapConfiguration>(resource).name();
  }

private:
  struct LastConfigInfo {
    uint64_t last_config_hash_;
    std::string last_config_version_;
  };

  void remove(
      ExtensionConfig& cfg
      ) {
        tap_extension_configs_.erase(&cfg);
      }
  void add(
      ExtensionConfig& cfg
      ) {
        tap_extension_configs_.insert(&cfg);
      }

  TdsTapConfigSubscription(
      const envoy::config::common::tap::v2alpha::CommonExtensionConfig_TDSConfig& tds,
      const uint64_t manager_identifier,
      TapConfigProviderManagerImpl& tap_config_provider_manager,



      const std::string& stat_prefix,
      Stats::Scope& stats,
      Upstream::ClusterManager& cluster_Manager,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher&  main_thread_dispatcher,
      Envoy::Runtime::RandomGenerator& random,
      Api::Api& api
      );

  std::unique_ptr<Envoy::Config::Subscription> subscription_;
  const std::string tap_config_name_;
  Init::TargetImpl init_target_;
  Stats::ScopePtr scope_;
  TdsStats stats_;
  TapConfigProviderManagerImpl& tap_config_provider_manager_;
  const uint64_t manager_identifier_;
  SystemTime last_updated_;
  absl::optional<LastConfigInfo> config_info_;
  envoy::service::tap::v2alpha::TapConfig tap_config_proto_;
  std::unordered_set<ExtensionConfig*> tap_extension_configs_;
  Api::Api& api_;

  friend class TapConfigProviderManagerImpl;
  friend class TdsTapConfigSubscriptionHandleImpl;
};

typedef std::shared_ptr<TdsTapConfigSubscription> TdsTapConfigSubscriptionSharedPtr;


class TdsTapConfigSubscriptionHandleImpl : public TdsTapConfigSubscriptionHandle {
public:
TdsTapConfigSubscriptionHandleImpl(const TdsTapConfigSubscriptionHandleImpl&) = delete;
TdsTapConfigSubscriptionHandleImpl(TdsTapConfigSubscriptionSharedPtr sub,
  ExtensionConfig& config);
  ~TdsTapConfigSubscriptionHandleImpl() override;
private:
  TdsTapConfigSubscriptionSharedPtr sub_;
  ExtensionConfig& config_;
};


class TapConfigProviderManagerImpl : public TapConfigProviderManager,
                                       public Singleton::Instance {
public:
      
  TapConfigProviderManagerImpl(Server::Admin& admin, Init::Manager* init_manager = nullptr): admin_(admin), init_manager_(init_manager) {
    // keep admin as we'll use it for admin endpoint.
    static_cast<void>(admin_);
  }
  ~TapConfigProviderManagerImpl(){
    static_cast<void>(1);
  }

  // std::unique_ptr<envoy::admin::v2alpha::RoutesConfigDump> dumpRouteConfigs() const;

  // RouteConfigProviderManager
  TdsTapConfigSubscriptionHandlePtr subscribeTap(
      const envoy::config::common::tap::v2alpha::CommonExtensionConfig_TDSConfig& tds,
    
      /* Server::Configuration::FactoryContext& factory_context  be explicit here ... */
      // const std::string& stat_prefix
      Extensions::Common::Tap::ExtensionConfig& ptr,


      const std::string& stat_prefix,
      Stats::Scope& stats,
      Upstream::ClusterManager& cluster_Manager,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher&  main_thread_dispatcher,
      Envoy::Runtime::RandomGenerator& random,
      Api::Api& api


      ) override;

private:

  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  std::unordered_map<uint64_t, std::weak_ptr<TdsTapConfigSubscription>>
      tap_config_subscriptions_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  Server::Admin& admin_;
  Init::Manager* init_manager_{};


  friend class TdsTapConfigSubscription;
};

}
}
}
}