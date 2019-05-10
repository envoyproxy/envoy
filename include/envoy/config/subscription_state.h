#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

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
  virtual ~SubscriptionState() {}

  void setInitFetchTimeout(Event::Dispatcher& dispatcher) PURE;

  void pause() PURE;
  void resume() PURE;
  bool paused() const PURE;

  // Update which resources we're interested in subscribing to.
  void updateResourceInterest(const std::set<std::string>& update_to_these_names) PURE;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const PURE;

  void markStreamFresh() PURE;

  // Argument should have been static_cast from GrpcStream's ResponseProto type.
  UpdateAck handleResponse(const Protobuf::Message& message) PURE;

  void handleEstablishmentFailure() PURE;

  // Will have been static_cast from GrpcStream's RequestProto type, and should be static_cast back before handing to GrpcStream::sendMessage.
  Protobuf::Message getNextRequestAckless() PURE;
  // Will have been static_cast from GrpcStream's RequestProto type, and should be static_cast back before handing to GrpcStream::sendMessage.
  Protobuf::Message getNextRequestWithAck(const UpdateAck& ack) PURE;
};

class SubscriptionStateFactory {
public:
  SubscriptionState makeSubscriptionState(const std::string& type_url, const std::set<std::string>& resource_names,
                         SubscriptionCallbacks& callbacks, std::chrono::milliseconds init_fetch_timeout, SubscriptionStats& stats) PURE;
};

} // namespace Config
} // namespace Envoy
