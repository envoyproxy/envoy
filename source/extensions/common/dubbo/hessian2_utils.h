#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/strings/string_view.h"
#include "hessian2/basic_codec/object_codec.hpp"
#include "hessian2/codec.hpp"
#include "hessian2/object.hpp"
#include "hessian2/reader.hpp"
#include "hessian2/writer.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class Hessian2Utils {
public:
  static uint32_t getParametersNumber(const std::string& parameters_type);
};

class BufferWriter : public Hessian2::Writer {
public:
  BufferWriter(Envoy::Buffer::Instance& buffer) : buffer_(buffer) {}

  // Hessian2::Writer
  void rawWrite(const void* data, uint64_t size) override;
  void rawWrite(absl::string_view data) override;

private:
  Envoy::Buffer::Instance& buffer_;
};

class BufferReader : public Hessian2::Reader {
public:
  BufferReader(Envoy::Buffer::Instance& buffer, uint64_t initial_offset = 0) : buffer_(buffer) {
    initial_offset_ = initial_offset;
  }

  // Hessian2::Reader
  uint64_t length() const override { return buffer_.length(); }
  void rawReadNBytes(void* data, size_t len, size_t peek_offset) override;

private:
  Envoy::Buffer::Instance& buffer_;
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
