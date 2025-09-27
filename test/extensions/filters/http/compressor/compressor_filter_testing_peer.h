#pragma once

#include "source/extensions/filters/http/compressor/compressor_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

class CompressorFilterTestingPeer {
public:
  static std::string contentEncoding(const CompressorFilter& filter) {
    return filter.getContentEncoding();
  }

  static Envoy::Compression::Compressor::CompressorFactory&
  compressorFactory(const CompressorFilter& filter) {
    return filter.getCompressorFactory();
  }
};

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
