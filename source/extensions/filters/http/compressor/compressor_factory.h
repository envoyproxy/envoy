#pragma once

#include "envoy/compressor/compressor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

class CompressorFactory {
public:
  virtual ~CompressorFactory() = default;

  virtual Envoy::Compressor::CompressorPtr createCompressor() PURE;
  virtual const std::string& statsPrefix() const PURE;
  virtual const std::string& contentEncoding() const PURE;
};

using CompressorFactoryPtr = std::unique_ptr<CompressorFactory>;

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
