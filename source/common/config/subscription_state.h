#pragma once

#include <memory>
#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

struct UpdateAck {
  UpdateAck(absl::string_view nonce, absl::string_view type_url)
      : nonce_(nonce), type_url_(type_url) {}
  std::string nonce_;
  std::string type_url_;
  ::google::rpc::Status error_detail_;
};

class SubscriptionState {
public:
  virtual ~SubscriptionState() = default;

  // Update which resources we're interested in subscribing to.
  virtual void updateResourceInterest(const std::set<std::string>& update_to_these_names) PURE;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  // Argument should have been static_cast from GrpcStream's ResponseProto type.
  virtual UpdateAck handleResponse(const Protobuf::Message& message) PURE;

  virtual void handleEstablishmentFailure() PURE;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  void* getNextRequestAckless();
  // The WithAck version first calls the Ackless version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  void* getNextRequestWithAck(const UpdateAck& ack);
};

class SubscriptionStateFactory {
public:
  virtual ~SubscriptionStateFactory() = default;
  virtual SubscriptionState makeSubscriptionState(const std::string& type_url,
                                                  const std::set<std::string>& resource_names,
                                                  SubscriptionCallbacks& callbacks,
                                                  std::chrono::milliseconds init_fetch_timeout,
                                                  SubscriptionStats& stats) PURE;
};

} // namespace Config
} // namespace Envoy
