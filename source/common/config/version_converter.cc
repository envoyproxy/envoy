#include "common/config/version_converter.h"

#include "common/config/api_type_oracle.h"

namespace Envoy {
namespace Config {

namespace {

// Reinterpret a Protobuf message as another Protobuf message by converting to
// wire format and back. This only works for messages that can be effectively
// duck typed this way, e.g. with a subtype relationship modulo field name.
void wireCast(const Protobuf::Message& src, Protobuf::Message& dst) {
  dst.ParseFromString(src.SerializeAsString());
}

} // namespace

void VersionConverter::upgrade(const Protobuf::Message& prev_message,
                               Protobuf::Message& next_message) {
  wireCast(prev_message, next_message);
}

DowngradedMessagePtr VersionConverter::downgrade(const Protobuf::Message& message) {
  auto downgraded_message = std::make_unique<DowngradedMessage>();
  const Protobuf::Descriptor* prev_desc = ApiTypeOracle::getEarlierVersionDescriptor(message);
  if (prev_desc != nullptr) {
    downgraded_message->msg_.reset(
        downgraded_message->dynamic_msg_factory_.GetPrototype(prev_desc)->New());
    wireCast(message, *downgraded_message->msg_);
    return downgraded_message;
  }
  // Unnecessary copy, since the existing message is being treated as
  // "downgraded". However, we want to transfer an owned object, so this is the
  // best we can do.
  const Protobuf::Descriptor* desc = message.GetDescriptor();
  downgraded_message->msg_.reset(
      downgraded_message->dynamic_msg_factory_.GetPrototype(desc)->New());
  downgraded_message->msg_->MergeFrom(message);
  return downgraded_message;
}

} // namespace Config
} // namespace Envoy
