#pragma once

#include "envoy/compression/compressor/compressor.h"
#include "envoy/compression/compressor/config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Compression {
namespace Compressor {

class MockCompressor : public Compressor {
public:
  MockCompressor();
  ~MockCompressor() override;

  // Compressor::Compressor
  MOCK_METHOD(void, compress, (Buffer::Instance & buffer, State state));
};

class MockCompressorFactory : public CompressorFactory {
public:
  MockCompressorFactory();
  ~MockCompressorFactory() override;

  // Compressor::CompressorFactory
  MOCK_METHOD(CompressorPtr, createCompressor, ());
  MOCK_METHOD(const std::string&, statsPrefix, (), (const));
  MOCK_METHOD(const std::string&, contentEncoding, (), (const));

  const std::string stats_prefix_{"mock"};
  const std::string content_encoding_{"mock"};
};

} // namespace Compressor
} // namespace Compression
} // namespace Envoy
