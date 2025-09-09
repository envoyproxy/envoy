#pragma once

#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace ProtobufMessage {
namespace Helper {

std::unique_ptr<Protobuf::Message> typeUrlToMessage(absl::string_view type_url);

template <typename T>
absl::StatusOr<std::pair<std::unique_ptr<Protobuf::Message>, absl::string_view>>
convertTypedStruct(const Protobuf::Message& message) {
  auto* typed_struct = Protobuf::DynamicCastMessage<T>(&message);
  auto inner_message = typeUrlToMessage(typed_struct->type_url());
  absl::string_view target_type_url = typed_struct->type_url();
  // inner_message might be invalid as we did not previously check type_url during loading.
  if (inner_message != nullptr) {
#ifdef ENVOY_ENABLE_YAML
    MessageUtil::jsonConvert(typed_struct->value(), ProtobufMessage::getNullValidationVisitor(),
                             *inner_message);
#else
    return absl::InvalidArgumentError("JSON and YAML support compiled out.");
#endif
  }
  return std::make_pair(std::move(inner_message), target_type_url);
}

/**
 * RAII wrapper that push message to parents on construction and pop it on destruction.
 */
struct ScopedMessageParents {
  ScopedMessageParents(std::vector<const Protobuf::Message*>& parents,
                       const Protobuf::Message& message);

  ~ScopedMessageParents();

private:
  std::vector<const Protobuf::Message*>& parents_;
};

} // namespace Helper
} // namespace ProtobufMessage
} // namespace Envoy
