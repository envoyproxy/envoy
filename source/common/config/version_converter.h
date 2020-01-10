#pragma once

#include "common/protobuf/protobuf.h"

// Convenience macro for downgrading a message and obtaining a reference.
#define API_DOWNGRADE(msg) (*Config::VersionConverter::downgrade(msg)->msg_)

namespace Envoy {
namespace Config {

// A message that has been downgraded, e.g. from v3alpha to v2.
struct DowngradedMessage {
  // The dynamic message factory must outlive the message.
  Protobuf::DynamicMessageFactory dynamic_msg_factory_;

  // Downgraded message.
  std::unique_ptr<Protobuf::Message> msg_;
};

using DowngradedMessagePtr = std::unique_ptr<DowngradedMessage>;

class VersionConverter {
public:
  /**
   * Upgrade a message from an earlier to later version of the Envoy API. This
   * performs a simple wire-level reinterpretation of the fields. As a result of
   * shadow protos, earlier deprecated fields such as foo are materialized as
   * hidden_envoy_deprecated_foo.
   *
   * @param prev_message previous version message input.
   * @param next_message next version message to generate.
   */
  static void upgrade(const Protobuf::Message& prev_message, Protobuf::Message& next_message);

  // Downgrade a message to the previous version. If no previous version exists,
  // the given message is copied in the return value. This is not super
  // efficient, most uses are expected to be tests and performance agnostic
  // code.
  static DowngradedMessagePtr downgrade(const Protobuf::Message& message);
};

} // namespace Config
} // namespace Envoy
