#include "test/mocks/compression/decompressor/mocks.h"

using testing::ReturnRef;

namespace Envoy {
namespace Compression {
namespace Decompressor {

MockDecompressor::MockDecompressor() = default;
MockDecompressor::~MockDecompressor() = default;

MockDecompressorFactory::MockDecompressorFactory() {
  ON_CALL(*this, statsPrefix()).WillByDefault(ReturnRef(stats_prefix_));
  ON_CALL(*this, contentEncoding()).WillByDefault(ReturnRef(content_encoding_));
}

MockDecompressorFactory::~MockDecompressorFactory() = default;

} // namespace Decompressor
} // namespace Compression
} // namespace Envoy
