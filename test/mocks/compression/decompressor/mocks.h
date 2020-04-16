#pragma once

#include "envoy/compression/decompressor/decompressor.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Compression {
namespace Decompressor {

class MockDecompressor : public Decompressor {
public:
  MockDecompressor();
  ~MockDecompressor() override;

  // Decompressor::Decompressor
  MOCK_METHOD(void, decompress,
              (const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer));
};

} // namespace Decompressor
} // namespace Compression
} // namespace Envoy
