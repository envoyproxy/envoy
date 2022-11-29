#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/ttl.h"
#include "source/common/config/update_ack.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

class SubscriptionState {};

// Tracks the protocol state of an individual ongoing xDS-over-gRPC session, for a single type_url.
// There can be multiple SubscriptionStates active, one per type_url. They will all be
// blissfully unaware of each other's existence, even when their messages are being multiplexed
// together by ADS.
// This is the abstract parent class for both the delta and state-of-the-world xDS variants.
template <class RS, class RQ>
class BaseSubscriptionState : public SubscriptionState,
                              public Logger::Loggable<Logger::Id::config> {
public:
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  BaseSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                        Event::Dispatcher& dispatcher,
                        XdsResourcesDelegateOptRef xds_resources_delegate = absl::nullopt,
                        const std::string& target_xds_authority = "")
      : ttl_([this](const std::vector<std::string>& expired) { ttlExpiryCallback(expired); },
             dispatcher, dispatcher.timeSource()),
        type_url_(std::move(type_url)), callbacks_(callbacks), dispatcher_(dispatcher),
        xds_resources_delegate_(xds_resources_delegate),
        target_xds_authority_(target_xds_authority) {}

  virtual ~BaseSubscriptionState() = default;

  // Update which resources we're interested in subscribing to.
  virtual void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                          const absl::flat_hash_set<std::string>& cur_removed) PURE;

  void setDynamicContextChanged() { dynamic_context_changed_ = true; }
  void clearDynamicContextChanged() { dynamic_context_changed_ = false; }
  bool dynamicContextChanged() const { return dynamic_context_changed_; }

  void setControlPlaneIdentifier(const std::string& id) { control_plane_identifier_ = id; }
  std::string& controlPlaneIdentifier() { return control_plane_identifier_; }

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  UpdateAck handleResponse(const RS& response) {
    // We *always* copy the response's nonce into the next request, even if we're going to make that
    // request a NACK by setting error_detail.
    UpdateAck ack(response.nonce(), typeUrl());
    ENVOY_LOG(debug, "Handling response for {}", typeUrl());
    TRY_ASSERT_MAIN_THREAD { handleGoodResponse(response); }
    END_TRY
    catch (const EnvoyException& e) {
      handleBadResponse(e, ack);
    }
    previously_fetched_data_ = true;
    return ack;
  }

  virtual void handleEstablishmentFailure() {
    ENVOY_LOG(debug, "SubscriptionState establishment failed for {}", typeUrl());
    callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure,
                                     nullptr);
  }

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  std::unique_ptr<RQ> getNextRequestAckless() { return getNextRequestInternal(); }

  // The WithAck version first calls the ack-less version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  std::unique_ptr<RQ> getNextRequestWithAck(const UpdateAck& ack) {
    auto request = getNextRequestInternal();
    request->set_response_nonce(ack.nonce_);
    ENVOY_LOG(debug, "ACK for {} will have nonce {}", typeUrl(), ack.nonce_);
    if (ack.error_detail_.code() != Grpc::Status::WellKnownGrpcStatus::Ok) {
      // Don't needlessly make the field present-but-empty if status is ok.
      request->mutable_error_detail()->CopyFrom(ack.error_detail_);
    }
    return request;
  }

  virtual void ttlExpiryCallback(const std::vector<std::string>& type_url) PURE;

protected:
  virtual std::unique_ptr<RQ> getNextRequestInternal() PURE;
  virtual void handleGoodResponse(const RS& message) PURE;
  void handleBadResponse(const EnvoyException& e, UpdateAck& ack) {
    // Note that error_detail being set is what indicates that a (Delta)DiscoveryRequest is a NACK.
    ack.error_detail_.set_code(Grpc::Status::WellKnownGrpcStatus::Internal);
    ack.error_detail_.set_message(Config::Utility::truncateGrpcStatusMessage(e.what()));
    ENVOY_LOG(warn, "Config for {} rejected: {}", typeUrl(), e.what());
    callbacks().onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
  }

  std::string typeUrl() const { return type_url_; }
  UntypedConfigUpdateCallbacks& callbacks() const { return callbacks_; }

  TtlManager ttl_;
  const std::string type_url_;
  // callbacks_ is expected (outside of tests) to be a WatchMap.
  UntypedConfigUpdateCallbacks& callbacks_;
  Event::Dispatcher& dispatcher_;
  bool dynamic_context_changed_{};
  std::string control_plane_identifier_{};
  XdsResourcesDelegateOptRef xds_resources_delegate_;
  const std::string target_xds_authority_;
  bool previously_fetched_data_{};
};

template <class T> class SubscriptionStateFactory {
public:
  virtual ~SubscriptionStateFactory() = default;
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  virtual std::unique_ptr<T>
  makeSubscriptionState(const std::string& type_url, UntypedConfigUpdateCallbacks& callbacks,
                        OpaqueResourceDecoderSharedPtr resource_decoder,
                        XdsResourcesDelegateOptRef xds_resources_delegate = absl::nullopt,
                        const std::string& target_xds_authority = "") PURE;
};

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
