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

  virtual void pause() PURE;
  virtual void resume() PURE;
  virtual bool paused() const PURE;

  // Update which resources we're interested in subscribing to.
  virtual void updateResourceInterest(const std::set<std::string>& update_to_these_names) PURE;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  // Argument should have been static_cast from GrpcStream's ResponseProto type.
  virtual UpdateAck handleResponse(const Protobuf::Message& message) PURE;

  virtual void handleEstablishmentFailure() PURE;

  // Will have been static_cast from GrpcStream's RequestProto type, and should be static_cast back
  // before handing to GrpcStream::sendMessage.
  virtual Protobuf::Message getNextRequestAckless() PURE;
  // Will have been static_cast from GrpcStream's RequestProto type, and should be static_cast back
  // before handing to GrpcStream::sendMessage.
  virtual Protobuf::Message getNextRequestWithAck(const UpdateAck& ack) PURE;
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
