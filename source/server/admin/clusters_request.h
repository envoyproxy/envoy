#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/server/admin.h"

namespace Envoy {
namespace Server {

// Captures context for a streaming request, implementing the Admin::Request interface.
class ClustersRequest : public Admin::Request {
public:
  static constexpr uint64_t DefaultChunkSize = 2 << 20; // 2 MB

  Http::Code start(Http::ResponseHeaderMap& response_headers) override;
  bool nextChunk(Buffer::Instance& response) override;

private:
  uint64_t chunk_size_{DefaultChunkSize};
};

} // namespace Server
} // namespace Envoy
