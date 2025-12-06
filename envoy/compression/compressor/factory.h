#pragma once

#include "envoy/compression/compressor/compressor.h"

namespace Envoy {
namespace Compression {
namespace Compressor {

class CompressorFactory {
public:
  virtual ~CompressorFactory() = default;

  // Create a new compressor instance. This should be thread-safe.
  virtual CompressorPtr createCompressor() PURE;
  virtual const std::string& statsPrefix() const PURE;
  virtual const std::string& contentEncoding() const PURE;
};

using CompressorFactoryPtr = std::unique_ptr<CompressorFactory>;

} // namespace Compressor
} // namespace Compression
} // namespace Envoy
