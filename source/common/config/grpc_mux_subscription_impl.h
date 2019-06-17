#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Config {

/**
 * Adapter from typed Subscription to untyped GrpcMux. Also handles per-xDS API stats/logging.
 */
class GrpcMuxSubscriptionImpl : public Subscription,
                                GrpcMuxCallbacks,
                                Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxSubscriptionImpl(GrpcMux& grpc_mux, SubscriptionCallbacks& callbacks,
                          SubscriptionStats stats, absl::string_view type_url,
                          Event::Dispatcher& dispatcher,
                          std::chrono::milliseconds init_fetch_timeout);

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResources(const std::set<std::string>& update_to_these_names) override;

  // Config::GrpcMuxCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override;

private:
  void disableInitFetchTimeoutTimer();

  GrpcMux& grpc_mux_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  const std::string type_url_;
  GrpcMuxWatchPtr watch_{};
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
};

} // namespace Config
} // namespace Envoy
