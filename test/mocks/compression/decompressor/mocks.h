#pragma once

#include "envoy/compression/decompressor/config.h"
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

class MockDecompressorFactory : public DecompressorFactory {
public:
  MockDecompressorFactory();
  ~MockDecompressorFactory() override;

  // Decompressor::DecompressorFactory
  MOCK_METHOD(DecompressorPtr, createDecompressor, (const std::string&));
  MOCK_METHOD(const std::string&, statsPrefix, (), (const));
  MOCK_METHOD(const std::string&, contentEncoding, (), (const));

  const std::string stats_prefix_{"mock"};
  const std::string content_encoding_{"mock"};
};

} // namespace Decompressor
} // namespace Compression
} // namespace Envoy
