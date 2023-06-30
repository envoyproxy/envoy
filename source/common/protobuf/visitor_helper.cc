#include "source/common/protobuf/visitor_helper.h"

namespace Envoy {
namespace ProtobufMessage {
namespace Helper {

std::unique_ptr<Protobuf::Message> typeUrlToMessage(absl::string_view type_url) {
  const absl::string_view inner_type_name = TypeUtil::typeUrlToDescriptorFullName(type_url);
  const Protobuf::Descriptor* inner_descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          std::string(inner_type_name));
  if (inner_descriptor == nullptr) {
    return nullptr;
  }
  auto* inner_message_prototype =
      Protobuf::MessageFactory::generated_factory()->GetPrototype(inner_descriptor);
  return std::unique_ptr<Protobuf::Message>(inner_message_prototype->New());
}

ScopedMessageParents::ScopedMessageParents(std::vector<const Protobuf::Message*>& parents,
                                           const Protobuf::Message& message)
    : parents_(parents) {
  parents_.push_back(&message);
}

ScopedMessageParents::~ScopedMessageParents() { parents_.pop_back(); }

} // namespace Helper
} // namespace ProtobufMessage
} // namespace Envoy
