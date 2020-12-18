#pragma once

#include <memory>

#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Config {

/**
 * Adapter from typed Subscription to untyped GrpcMux. Also handles per-xDS API stats/logging.
 */
class LegacyGrpcSubscriptionImpl : public Subscription,
                                   SubscriptionCallbacks,
                                   Logger::Loggable<Logger::Id::config> {
public:
  LegacyGrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux, SubscriptionCallbacks& callbacks,
                             OpaqueResourceDecoder& resource_decoder, SubscriptionStats stats,
                             absl::string_view type_url, Event::Dispatcher& dispatcher,
                             std::chrono::milliseconds init_fetch_timeout, bool is_aggregated);
  ~LegacyGrpcSubscriptionImpl() override;

  // Config::Subscription
  void start(const std::set<std::string>& resource_names,
             const bool use_namespace_matching = false) override;
  void updateResourceInterest(const std::set<std::string>& update_to_these_names,
                              const bool use_namespace_matching) override;
  void requestOnDemandUpdate(const std::set<std::string>& add_these_names) override;
  // Config::SubscriptionCallbacks (all pass through to callbacks_!)
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override;

  GrpcMuxSharedPtr getGrpcMuxForTest() { return grpc_mux_; }

  ScopedResume pause();

private:
  void disableInitFetchTimeoutTimer();

  GrpcMuxSharedPtr grpc_mux_;
  SubscriptionCallbacks& callbacks_;
  OpaqueResourceDecoder& resource_decoder_;
  SubscriptionStats stats_;
  const std::string type_url_;
  Watch* watch_{nullptr};
  Event::Dispatcher& dispatcher_;
  // NOTE: if another subscription of the same type_url has already been started, this value will be
  // ignored in favor of the other subscription's.
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  const bool is_aggregated_;
};

} // namespace Config
} // namespace Envoy
