#include "source/extensions/common/dubbo/message.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/dubbo/hessian2_utils.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

void RequestContent::initialize(Buffer::Instance& buffer, uint64_t length) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");

  content_buffer_.move(buffer, length);

  // Clear the types, arguments and attachments.
  types_.clear();
  argvs_.clear();
  attachs_.clear();

  // Set both decoded and updated to false since the content has been initialized
  // by raw buffer.
  decoded_ = false;
  updated_ = false;
}

void RequestContent::initialize(std::string&& types, ArgumentVec&& argvs, Attachments&& attachs) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");

  // Set the types, arguments and attachments.
  types_ = std::move(types);
  argvs_ = std::move(argvs);
  attachs_ = std::move(attachs);

  // Encode the types, arguments and attachments into the content buffer.
  encodeEverything();

  // Set decoded to true since the content has been initialized by types,
  // arguments and attachments.
  decoded_ = true;
  updated_ = false;
}

const Buffer::Instance& RequestContent::buffer() {
  // Ensure the attachments in the buffer is latest.

  if (content_buffer_.length() == 0) {
    encodeEverything();
  } else {
    encodeAttachments();
  }

  return content_buffer_;
}

void RequestContent::bufferMoveTo(Buffer::Instance& buffer) { buffer.move(content_buffer_); }

const ArgumentVec& RequestContent::arguments() {
  lazyDecode();

  return argvs_;
}

const Attachments& RequestContent::attachments() {
  lazyDecode();

  return attachs_;
}

void RequestContent::setAttachment(absl::string_view key, absl::string_view val) {
  lazyDecode();

  updated_ = true;
  attachs_[key] = val;
}

void RequestContent::delAttachment(absl::string_view key) {
  lazyDecode();

  updated_ = true;
  attachs_.erase(key);
}

void RequestContent::lazyDecode() {
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
        } else {
          ENVOY_LOG(error, "Cannot parse RpcInvocation parameter types from buffer");
          handleBrokenValue();
          return;
        }
      } else if (direct_num >= 0) {
        number = direct_num;
      } else {
        ENVOY_LOG(error, "Invalid RpcInvocation parameter number {}", direct_num);
        handleBrokenValue();
        return;
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
        handleBrokenValue();
        return;
      }
    }
  } else {
    ENVOY_LOG(error, "Cannot parse RpcInvocation from buffer");
    handleBrokenValue();
    return;
  }

  // Record the size of the arguments in the content buffer. This is useful for
  // re-encoding the attachments.
  argvs_size_ = decoder.offset();

  // Handle the attachments.
  auto map = decoder.decode<Hessian2::Object>();
  if (map == nullptr || map->type() != Hessian2::Object::Type::UntypedMap) {
    return;
  }

  for (auto& [key, val] : map->toMutableUntypedMap().value().get()) {
    if (key->type() != Hessian2::Object::Type::String ||
        val->type() != Hessian2::Object::Type::String) {
      continue;
    }
    attachs_.emplace(std::move(key->toMutableString().value().get()),
                     std::move(val->toMutableString().value().get()));
  }
}

void RequestContent::encodeAttachments() {
  // Do nothing if the attachments have not been updated.
  if (!updated_) {
    return;
  }

  // Ensure the content has been decoded before re-encoding it.
  lazyDecode();

  const uint64_t buffer_length = content_buffer_.length();
  ASSERT(buffer_length > 0, "content buffer is empty");

  // The size of arguments will be set when doing lazyDecode() or encodeEverything().
  if (buffer_length < argvs_size_) {
    ENVOY_LOG(error, "arguments size {} is larger than content buffer {}", argvs_size_,
              buffer_length);
    handleBrokenValue();
    return;
  }

  // Create a new buffer to hold the re-encoded content.

  Buffer::OwnedImpl new_content_buffer;
  // Copy the types and arguments into the new buffer.
  new_content_buffer.move(content_buffer_, argvs_size_);

  // Encode the attachments into the new buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(new_content_buffer));
  new_content_buffer.writeByte('H');
  for (auto& [key, val] : attachs_) {
    encoder.encode(key);
    encoder.encode(val);
  }
  new_content_buffer.writeByte('Z');

  // Clear the content buffer and move the new buffer into it.
  content_buffer_.drain(content_buffer_.length());
  content_buffer_.move(new_content_buffer);

  updated_ = false;
}

void RequestContent::encodeEverything() {
  ASSERT(content_buffer_.length() == 0, "content buffer contains something");

  // Encode the types, arguments and attachments into the content buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(content_buffer_));

  // Encode the types into the content buffer first.
  if (!types_.empty()) {
    encoder.encode(types_);
  } else if (!argvs_.empty()) {
    encoder.encode(static_cast<int32_t>(argvs_.size()));
  } else {
    encoder.encode(types_);
  }

  // Encode the arguments into the content buffer.
  for (auto& arg : argvs_) {
    encoder.encode(*arg);
  }

  // Record the size of the arguments in the content buffer. This is useful for
  // re-encoding the attachments.
  argvs_size_ = content_buffer_.length();

  // Encode the attachments into the content buffer.
  content_buffer_.writeByte('H');
  for (auto& [key, val] : attachs_) {
    encoder.encode(key);
    encoder.encode(val);
  }
  content_buffer_.writeByte('Z');

  updated_ = false;
}

void RequestContent::handleBrokenValue() {
  // Because the lazy decoding is used, Envoy cannot reject the message with broken
  // content. Instead, it will reset the whole content to an empty state.

  // Clear everything.
  content_buffer_.drain(content_buffer_.length());
  types_.clear();
  argvs_.clear();
  attachs_.clear();

  // Encode everything.
  encodeEverything();

  decoded_ = true;
  updated_ = false;
}

void ResponseContent::initialize(Buffer::Instance& buffer, uint64_t length) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");
  content_buffer_.move(buffer, length);

  // Clear the result and attachments.
  result_ = nullptr;
  attachs_.clear();

  // Set both decoded and updated to false since the content has been initialized
  // by raw buffer.
  decoded_ = false;
  updated_ = false;
}

void ResponseContent::initialize(Hessian2::ObjectPtr&& value, Attachments&& attachs) {
  ASSERT(content_buffer_.length() == 0, "content buffer has been initialized");

  // Set the result and attachments.
  result_ = std::move(value);
  attachs_ = std::move(attachs);

  // Encode the result and attachments into the content buffer.
  encodeEverything();

  // Set decoded to true since the content has been initialized by result and attachments.
  decoded_ = true;
  updated_ = false;
}

const Buffer::Instance& ResponseContent::buffer() {
  // Ensure the attachments in the buffer is latest.
  if (content_buffer_.length() == 0) {
    encodeEverything();
  } else {
    encodeAttachments();
  }

  return content_buffer_;
}

void ResponseContent::bufferMoveTo(Buffer::Instance& buffer) { buffer.move(content_buffer_); }

const Hessian2::Object* ResponseContent::result() {
  lazyDecode();

  return result_.get();
}

const Attachments& ResponseContent::attachments() {
  lazyDecode();

  return attachs_;
}

void ResponseContent::setAttachment(absl::string_view key, absl::string_view val) {
  lazyDecode();

  updated_ = true;
  attachs_[key] = val;
}

void ResponseContent::delAttachment(absl::string_view key) {
  lazyDecode();

  updated_ = true;
  attachs_.erase(key);
}

void ResponseContent::lazyDecode() {
  if (decoded_) {
    return;
  }
  decoded_ = true;

  // Decode the content buffer into result and attachments.
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(content_buffer_));

  // Handle the result.
  result_ = decoder.decode<Hessian2::Object>();

  if (result_ == nullptr) {
    ENVOY_LOG(error, "Cannot parse RpcResult from buffer");
    handleBrokenValue();
    return;
  }

  // Record the size of the result in the content buffer. This is useful for
  // re-encoding the attachments.
  result_size_ = decoder.offset();

  // Handle the attachments.
  auto map = decoder.decode<Hessian2::Object>();
  if (map == nullptr || map->type() != Hessian2::Object::Type::UntypedMap) {
    return;
  }

  for (auto& [key, val] : map->toMutableUntypedMap().value().get()) {
    if (key->type() != Hessian2::Object::Type::String ||
        val->type() != Hessian2::Object::Type::String) {
      continue;
    }
    attachs_.emplace(std::move(key->toMutableString().value().get()),
                     std::move(val->toMutableString().value().get()));
  }
}

void ResponseContent::encodeAttachments() {
  if (!updated_) {
    return;
  }

  // Ensure the content has been decoded before re-encoding it.
  lazyDecode();

  const uint64_t buffer_length = content_buffer_.length();
  ASSERT(buffer_length > 0, "content buffer is empty");

  if (buffer_length < result_size_) {
    ENVOY_LOG(error, "result size {} is larger than content buffer {}", result_size_,
              buffer_length);
    handleBrokenValue();
    return;
  }

  // Create a new buffer to hold the re-encoded content.
  Buffer::OwnedImpl new_content_buffer;

  // Copy the result into the new buffer.
  new_content_buffer.move(content_buffer_, result_size_);

  // Encode the attachments into the new buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(new_content_buffer));
  new_content_buffer.writeByte('H');
  for (auto& [key, val] : attachs_) {
    encoder.encode(key);
    encoder.encode(val);
  }
  new_content_buffer.writeByte('Z');

  // Clear the content buffer and move the new buffer into it.
  content_buffer_.drain(content_buffer_.length());
  content_buffer_.move(new_content_buffer);

  updated_ = false;
}

void ResponseContent::encodeEverything() {
  ASSERT(content_buffer_.length() == 0, "content buffer contains something");

  // Encode the result and attachments into the content buffer.
  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(content_buffer_));
  if (result_ == nullptr) {
    content_buffer_.writeByte('N');
  } else {
    encoder.encode(*result_);
  }

  // Record the size of the result in the content buffer. This is useful for
  // re-encoding the attachments.
  result_size_ = content_buffer_.length();

  content_buffer_.writeByte('H');
  for (auto& [key, val] : attachs_) {
    encoder.encode(key);
    encoder.encode(val);
  }
  content_buffer_.writeByte('Z');

  updated_ = false;
}

void ResponseContent::handleBrokenValue() {
  // Because the lazy decoding is used, Envoy cannot reject the message with broken
  // content. Instead, it will reset the whole content to an empty state.

  // Clear everything.
  content_buffer_.drain(content_buffer_.length());
  result_ = nullptr;
  attachs_.clear();

  // Encode everything.
  encodeEverything();

  decoded_ = true;
  updated_ = false;
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
