#include "source/common/protobuf/visitor.h"

#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

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

template <typename T>
std::pair<std::unique_ptr<Protobuf::Message>, absl::string_view>
convertTypedStruct(const Protobuf::Message& message) {
  auto* typed_struct = Protobuf::DynamicCastToGenerated<T>(&message);
  auto inner_message = typeUrlToMessage(typed_struct->type_url());
  absl::string_view target_type_url = typed_struct->type_url();
  // inner_message might be invalid as we did not previously check type_url during loading.
  if (inner_message != nullptr) {
    MessageUtil::jsonConvert(typed_struct->value(), ProtobufMessage::getNullValidationVisitor(),
                             *inner_message);
  }
  return {std::move(inner_message), target_type_url};
}

/**
 * RAII wrapper that push message to parents on construction and pop it on destruction.
 */
struct ScopedMessageParents {
  ScopedMessageParents(std::vector<const Protobuf::Message*>& parents,
                       const Protobuf::Message& message)
      : parents_(parents) {
    parents_.push_back(&message);
  }

  ~ScopedMessageParents() { parents_.pop_back(); }

private:
  std::vector<const Protobuf::Message*>& parents_;
};

void traverseMessageWorker(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                           std::vector<const Protobuf::Message*>& parents,
                           bool was_any_or_top_level, bool recurse_into_any) {
  visitor.onMessage(message, parents, was_any_or_top_level);

  // If told to recurse into Any messages, do that here and skip the rest of the function.
  if (recurse_into_any) {
    std::unique_ptr<Protobuf::Message> inner_message;
    absl::string_view target_type_url;

    if (message.GetDescriptor()->full_name() == "google.protobuf.Any") {
      auto* any_message = Protobuf::DynamicCastToGenerated<ProtobufWkt::Any>(&message);
      inner_message = typeUrlToMessage(any_message->type_url());
      target_type_url = any_message->type_url();
      // inner_message must be valid as parsing would have already failed to load if there was an
      // invalid type_url.
      MessageUtil::unpackTo(*any_message, *inner_message);
    } else if (message.GetDescriptor()->full_name() == "xds.type.v3.TypedStruct") {
      std::tie(inner_message, target_type_url) =
          convertTypedStruct<xds::type::v3::TypedStruct>(message);
    } else if (message.GetDescriptor()->full_name() == "udpa.type.v1.TypedStruct") {
      std::tie(inner_message, target_type_url) =
          convertTypedStruct<udpa::type::v1::TypedStruct>(message);
    }

    if (inner_message != nullptr) {
      // Push the Any message as a wrapper.
      ScopedMessageParents scoped_parents(parents, message);
      traverseMessageWorker(visitor, *inner_message, parents, true, recurse_into_any);
      return;
    } else if (!target_type_url.empty()) {
      throw EnvoyException(fmt::format("Invalid type_url '{}' during traversal", target_type_url));
    }
  }

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    visitor.onField(message, *field);

    // If this is a message, recurse in to the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      ScopedMessageParents scoped_parents(parents, message);

      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMessageWorker(visitor, reflection->GetRepeatedMessage(message, field, j), parents,
                                false, recurse_into_any);
        }
      } else if (reflection->HasField(message, field)) {
        traverseMessageWorker(visitor, reflection->GetMessage(message, field), parents, false,
                              recurse_into_any);
      }
    }
  }
}

} // namespace

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     bool recurse_into_any) {
  std::vector<const Protobuf::Message*> parents;
  traverseMessageWorker(visitor, message, parents, true, recurse_into_any);
}

} // namespace ProtobufMessage
} // namespace Envoy
