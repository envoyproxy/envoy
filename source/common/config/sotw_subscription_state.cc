#include "common/config/sotw_subscription_state.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

SotwSubscriptionState::SotwSubscriptionState(std::string type_url,
                                             UntypedConfigUpdateCallbacks& callbacks,
                                             std::chrono::milliseconds init_fetch_timeout,
                                             Event::Dispatcher& dispatcher)
    : SubscriptionState(std::move(type_url), callbacks, init_fetch_timeout, dispatcher) {} 

SotwSubscriptionState::~SotwSubscriptionState() = default;

SotwSubscriptionStateFactory::SotwSubscriptionStateFactory(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

SotwSubscriptionStateFactory::~SotwSubscriptionStateFactory() = default;

std::unique_ptr<SubscriptionState>
SotwSubscriptionStateFactory::makeSubscriptionState(const std::string& type_url,
                                                    UntypedConfigUpdateCallbacks& callbacks,
                                                    std::chrono::milliseconds init_fetch_timeout) {
  return std::make_unique<SotwSubscriptionState>(type_url, callbacks, init_fetch_timeout,
                                                 dispatcher_);
}

void SotwSubscriptionState::updateSubscriptionInterest(const std::set<std::string>& cur_added,
                                                       const std::set<std::string>& cur_removed) {
  for (const auto& a : cur_added) {
    names_tracked_.insert(a);
  }
  for (const auto& r : cur_removed) {
    names_tracked_.erase(r);
  }
  if (!cur_added.empty() || !cur_removed.empty()) {
    update_pending_ = true;
  }
}

// Not having sent any requests yet counts as an "update pending" since you're supposed to resend
// the entirety of your interest at the start of a stream, even if nothing has changed.
bool SotwSubscriptionState::subscriptionUpdatePending() const { return update_pending_; }

void SotwSubscriptionState::markStreamFresh() {
  last_good_version_info_ = absl::nullopt;
  last_good_nonce_ = absl::nullopt;
  update_pending_ = true;
}

UpdateAck SotwSubscriptionState::handleResponse(const void* response_proto_ptr) {
  auto* response =
      static_cast<const envoy::service::discovery::v3::DiscoveryResponse*>(response_proto_ptr);
  // We *always* copy the response's nonce into the next request, even if we're going to make that
  // request a NACK by setting error_detail.
  UpdateAck ack(response->nonce(), type_url());
  ENVOY_LOG(debug, "Handling response for {}", type_url());
  try {
    handleGoodResponse(*response);
  } catch (const EnvoyException& e) {
    handleBadResponse(e, ack);
  }
  return ack;
}

void SotwSubscriptionState::handleGoodResponse(
    const envoy::service::discovery::v3::DiscoveryResponse& message) {
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> non_heartbeat_resources;
  std::vector<envoy::service::discovery::v3::Resource> resources_with_ttl(message.resources().size());

  for (const auto& any : message.resources()) {
    if (!any.Is<envoy::service::discovery::v3::Resource>() &&
                    any.type_url() != message.type_url()) {
      throw EnvoyException(fmt::format("type URL {} embedded in an individual Any does not match "
                                       "the message-wide type URL {} in DiscoveryResponse {}",
                                       any.type_url(), message.type_url(),
                                       message.DebugString()));
    }

    // ttl changes (including removing of the ttl timer) are only done when an Any is wrapped in a Resource (which contains ttl duration).
    if (any.Is<envoy::service::discovery::v3::Resource>()) {
      resources_with_ttl.emplace(resources_with_ttl.end());
      MessageUtil::unpackTo(any, resources_with_ttl.back());
    
      if (isHeartbeatResource(resources_with_ttl.back(), message.version_info())) {
        continue;
      }
    }
    non_heartbeat_resources.Add()->CopyFrom(any);
  }

  {
    const auto scoped_update = ttl_.scopedTtlUpdate();
    for (auto& resource : resources_with_ttl) {
      setResourceTtl(resource);
    }
  }

  callbacks().onConfigUpdate(non_heartbeat_resources, message.version_info());
  // Now that we're passed onConfigUpdate() without an exception thrown, we know we're good.
  last_good_version_info_ = message.version_info();
  last_good_nonce_ = message.nonce();
  ENVOY_LOG(debug, "Config update for {} (version {}) accepted with {} resources", type_url(),
            message.version_info(), message.resources().size());
}

void SotwSubscriptionState::handleBadResponse(const EnvoyException& e, UpdateAck& ack) {
  // Note that error_detail being set is what indicates that a DeltaDiscoveryRequest is a NACK.
  ack.error_detail_.set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
  ack.error_detail_.set_message(Config::Utility::truncateGrpcStatusMessage(e.what()));
  ENVOY_LOG(warn, "gRPC state-of-the-world config for {} rejected: {}", type_url(), e.what());
  callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
}

void SotwSubscriptionState::handleEstablishmentFailure() {
  ENVOY_LOG(debug, "SotwSubscriptionState establishment failed for {}", type_url());
  callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure,
                                   nullptr);
}

envoy::service::discovery::v3::DiscoveryRequest* SotwSubscriptionState::getNextRequestInternal() {
  auto* request = new envoy::service::discovery::v3::DiscoveryRequest;
  request->set_type_url(type_url());

  std::copy(names_tracked_.begin(), names_tracked_.end(),
            Protobuf::RepeatedFieldBackInserter(request->mutable_resource_names()));
  if (last_good_version_info_.has_value()) {
    request->set_version_info(last_good_version_info_.value());
  }
  // Default response_nonce to the last known good one. If we are being called by
  // getNextRequestWithAck(), this value will be overwritten.
  if (last_good_nonce_.has_value()) {
    request->set_response_nonce(last_good_nonce_.value());
  }

  update_pending_ = false;
  return request;
}

void* SotwSubscriptionState::getNextRequestAckless() { return getNextRequestInternal(); }

void* SotwSubscriptionState::getNextRequestWithAck(const UpdateAck& ack) {
  envoy::service::discovery::v3::DiscoveryRequest* request = getNextRequestInternal();
  request->set_response_nonce(ack.nonce_);
  ENVOY_LOG(debug, "ACK for {} will have nonce {}", type_url(), ack.nonce_);
  if (ack.error_detail_.code() != Grpc::Status::WellKnownGrpcStatus::Ok) {
    // Don't needlessly make the field present-but-empty if status is ok.
    request->mutable_error_detail()->CopyFrom(ack.error_detail_);
  }
  return request;
}

void SotwSubscriptionState::setResourceTtl(const envoy::service::discovery::v3::Resource& resource) {
  if (resource.has_ttl()) {
    ttl_.add(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(resource.ttl())),
             resource.name());
  } else {
    ttl_.clear(resource.name());
  }
}

void SotwSubscriptionState::ttlExpiryCallback(const std::vector<std::string>& expired) {
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  for (const auto& resource : expired) {
    removed_resources.Add(std::string(resource));
  }
  callbacks().onConfigUpdate({}, removed_resources, "");
}

bool SotwSubscriptionState::isHeartbeatResource(const envoy::service::discovery::v3::Resource& resource, const std::string& version) {
  return !resource.has_resource() && last_good_version_info_.has_value() &&
           version == last_good_version_info_.value();
}

} // namespace Config
} // namespace Envoy
