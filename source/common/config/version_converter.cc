#include "common/config/version_converter.h"

#include "common/config/api_type_oracle.h"

namespace Envoy {
namespace Config {

void VersionConverter::upgrade(const Protobuf::Message& prev_message,
                               Protobuf::Message& next_message) {
  std::string s;
  prev_message.SerializeToString(&s);
  next_message.ParseFromString(s);
}

DowngradedMessagePtr VersionConverter::downgrade(const Protobuf::Message& message) {
  auto downgraded_message = std::make_unique<DowngradedMessage>();
  const Protobuf::Descriptor* prev_desc = ApiTypeOracle::getEarlierVersionDescriptor(message);
  if (prev_desc != nullptr) {
    downgraded_message->msg_.reset(downgraded_message->dmf_.GetPrototype(prev_desc)->New());
    std::string s;
    message.SerializeToString(&s);
    downgraded_message->msg_->ParseFromString(s);
    return downgraded_message;
  }
  // Unnecessary copy..
  const Protobuf::Descriptor* desc = message.GetDescriptor();
  downgraded_message->msg_.reset(downgraded_message->dmf_.GetPrototype(desc)->New());
  downgraded_message->msg_->MergeFrom(message);
  return downgraded_message;
}

} // namespace Config
} // namespace Envoy
