#include "test/mocks/compression/compressor/mocks.h"

using testing::ReturnRef;

namespace Envoy {
namespace Compression {
namespace Compressor {

MockCompressor::MockCompressor() = default;
MockCompressor::~MockCompressor() = default;

MockCompressorFactory::MockCompressorFactory() {
  ON_CALL(*this, statsPrefix()).WillByDefault(ReturnRef(stats_prefix_));
  ON_CALL(*this, contentEncoding()).WillByDefault(ReturnRef(content_encoding_));
}

MockCompressorFactory::~MockCompressorFactory() = default;

} // namespace Compressor
} // namespace Compression
} // namespace Envoy
