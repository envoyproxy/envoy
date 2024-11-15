#include "contrib/sip_proxy/filters/network/source/encoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

void EncoderImpl::encode(const MessageMetadataSharedPtr& metadata, Buffer::Instance& out) {
  std::string output = "";
  std::string& raw_msg = metadata->rawMsg();
  std::sort(metadata->operationList().begin(), metadata->operationList().end());

  size_t previous_position = 0;
  for (auto& operation : metadata->operationList()) {
    switch (operation.type_) {
    case OperationType::Insert: {
      std::string value = absl::get<InsertOperationValue>(operation.value_).value_;
      if (value == ";ep=" || value == ",opaque=") {
        if (metadata->ep().has_value() && metadata->ep().value().length() > 0) {
          output += raw_msg.substr(previous_position, operation.position_ - previous_position);
          previous_position = operation.position_;

          output += absl::get<InsertOperationValue>(operation.value_).value_;
          if (value == ",opaque=") {
            output += "\"";
          }
          output += std::string(metadata->ep().value());
          if (value == ",opaque=") {
            output += "\"";
          }
        }
      } else {
        output += raw_msg.substr(previous_position, operation.position_ - previous_position);
        previous_position = operation.position_;

        output += absl::get<InsertOperationValue>(operation.value_).value_;
      }
      break;
    }
    case OperationType::Modify:
      output += raw_msg.substr(previous_position, operation.position_ - previous_position);
      previous_position = operation.position_;

      output += absl::get<ModifyOperationValue>(operation.value_).dest_;
      previous_position += absl::get<ModifyOperationValue>(operation.value_).src_length_;
      break;
    case OperationType::Delete:
      output += raw_msg.substr(previous_position, operation.position_ - previous_position);
      previous_position = operation.position_;

      previous_position += absl::get<DeleteOperationValue>(operation.value_).length_;
      break;
    default:
      break;
    }
  }

  output += raw_msg.substr(previous_position);
  out.add(output);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
