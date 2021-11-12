#pragma once

#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/new_delta_subscription_state.h"
#include "source/common/config/old_delta_subscription_state.h"

#include "absl/container/flat_hash_set.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Config {

using DeltaSubscriptionStateVariant =
    absl::variant<OldDeltaSubscriptionState, NewDeltaSubscriptionState>;

class DeltaSubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& watch_map,
                         const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher);

  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed);
  void setMustSendDiscoveryRequest();
  bool subscriptionUpdatePending() const;
  void markStreamFresh();
  UpdateAck handleResponse(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message);
  void handleEstablishmentFailure();
  envoy::service::discovery::v3::DeltaDiscoveryRequest getNextRequestAckless();
  envoy::service::discovery::v3::DeltaDiscoveryRequest getNextRequestWithAck(const UpdateAck& ack);

  DeltaSubscriptionState(const DeltaSubscriptionState&) = delete;
  DeltaSubscriptionState& operator=(const DeltaSubscriptionState&) = delete;

private:
  DeltaSubscriptionStateVariant state_;
};

} // namespace Config
} // namespace Envoy
