#include "source/extensions/common/dubbo/message.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

void RequestContent::initialize(Buffer::Instance& buffer, uint64_t length) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");
  content_buffer_.move(buffer, length);

  types_.clear();
  argvs_.clear();
  attachs_.clear();

  decoded_ = false;
  updated_ = false;
}

void RequestContent::encodeAll() {
  // Encode the types, arguments and attachments into the content buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(content_buffer_));

  // Encode the types into the content buffer first.
  encoder.encode(types_);

  // Encode the arguments into the content buffer.
  for (auto& arg : argvs_) {
    encoder.encode(*arg);
  }

  // Record the offset of the attachments in the content buffer. This is useful for re-encoding
  // the attachments when the content has been updated.
  argvs_size_ = content_buffer_.length();

  // Encode the attachments into the content buffer.
  content_buffer_.writeByte('H');
  for (auto& attach : attachs_) {
    encoder.encode(attach.first);
    encoder.encode(attach.second);
  }
  content_buffer_.writeByte('Z');
}

void RequestContent::initialize(std::string&& types, ArgumentArr&& argvs, Attachments&& attachs) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");

  types_ = std::move(types);
  argvs_ = std::move(argvs);
  attachs_ = std::move(attachs);

  encodeAll();

  decoded_ = true;
  updated_ = false;
}

Buffer::Instance& RequestContent::buffer() {
  // Try re-encoding the content if it has been updated. If the content isn't updated, the
  // encode() call is a no-op.
  encode();

  return content_buffer_;
}

void RequestContent::encode() {
  const uint64_t buffer_length = content_buffer_.length();

  if (buffer_length == 0) {
    encodeAll();
    return;
  }

  if (buffer_length < argvs_size_) {
    // This should never happen. Clear the content buffer and re-encode everything.
    ENVOY_LOG(error, "arguments size {} is larger than content buffer {}", argvs_size_,
              buffer_length);
    content_buffer_.drain(buffer_length);
    encodeAll();
    return;
  }

  if (!updated_) {
    return;
  }

  // Create a new buffer to hold the re-encoded content.

  Buffer::OwnedImpl new_content_buffer;
  // Copy the types and arguments into the new buffer.
  new_content_buffer.move(content_buffer_, argvs_size_);

  // Encode the attachments into the new buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(new_content_buffer));
  new_content_buffer.writeByte('H');
  for (auto& attach : attachs_) {
    encoder.encode(attach.first);
    encoder.encode(attach.second);
  }
  new_content_buffer.writeByte('Z');

  // Clear the content buffer and move the new buffer into it.
  content_buffer_.drain(content_buffer_.length());
  content_buffer_.move(new_content_buffer);

  updated_ = false;
}

ArgumentArr& RequestContent::arguments() {
  lazyDecode();

  return argvs_;
}

const Hessian2::Object* RequestContent::getArgument(size_t index) const {
  lazyDecode();

  if (index >= argvs_.size()) {
    return nullptr;
  }

  return argvs_[index].get();
}

Attachments& RequestContent::attachments() {
  lazyDecode();
  return attachs_;
}

void RequestContent::setAttachment(absl::string_view key, absl::string_view val) {
  lazyDecode();
  attachs_[key] = val;
}

absl::optional<absl::string_view> RequestContent::getAttachment(absl::string_view key) const {
  lazyDecode();

  auto it = attachs_.find(key);
  if (it != attachs_.end()) {
    return absl::string_view{it->second};
  }
  return {};
}

void RequestContent::lazyDecode() const {
  if (decoded_) {
    return;
  }
  decoded_ = true;

  // Decode the content buffer into types, arguments and attachments.
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(content_buffer_));

  // Handle the types and arguments.
  if (auto element = decoder.decode<Hessian2::Object>(); element != nullptr) {
    uint32_t number = 0;

    if (element->type() == Hessian2::Object::Type::Integer) {
      ASSERT(element->toInteger().has_value());
      if (int32_t direct_num = element->toInteger().value(); direct_num == -1) {
        if (auto types = decoder.decode<std::string>(); types != nullptr) {
          types_ = *types;
          number = Hessian2Utils::getParametersNumber(types_);
        }
      } else {
        ASSERT(direct_num >= 0);
        number = direct_num;
      }
    } else if (element->type() == Hessian2::Object::Type::String) {
      ASSERT(element->toString().has_value());
      types_ = element->toString().value().get();
      number = Hessian2Utils::getParametersNumber(types_);
    }

    for (uint32_t i = 0; i < number; i++) {
      if (auto result = decoder.decode<Hessian2::Object>(); result != nullptr) {
        argvs_.push_back(std::move(result));
      } else {
        ENVOY_LOG(error, "Cannot parse RpcInvocation parameter from buffer");
        break;
      }
    }
  }

  argvs_size_ = decoder.offset();

  // Handle the attachments.
  auto attachs = decoder.decode<Hessian2::Object>();
  if (attachs == nullptr || attachs->type() != Hessian2::Object::Type::UntypedMap) {
    return;
  }

  for (auto& [key, val] : attachs->toMutableUntypedMap().value().get()) {
    if (key->type() != Hessian2::Object::Type::String ||
        val->type() != Hessian2::Object::Type::String) {
      continue;
    }
    attachs_.emplace(std::move(key->toMutableString().value().get()),
                     std::move(val->toMutableString().value().get()));
  }
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
