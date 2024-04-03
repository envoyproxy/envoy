#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/clusters_params.h"
#include "source/server/admin/clusters_renderer.h"

namespace Envoy {
namespace Server {

// Captures context for a streaming request, implementing the Admin::Request interface.
class ClustersRequest : public Admin::Request {
public:
  static constexpr uint64_t DefaultChunkLimit = 2 << 20; // 2 MB

  ClustersRequest(uint64_t chunk_limit, Instance& server, Buffer::Instance& response,
                  const ClustersParams& params);

  Http::Code start(Http::ResponseHeaderMap& response_headers) override;
  bool nextChunk(Buffer::Instance& response) override;

private:
  using ClustersRendererPtr = std::unique_ptr<ClustersRenderer>;

  uint64_t chunk_limit_{DefaultChunkLimit};
  Server::Instance& server_;
  Buffer::Instance& response_;
  const ClustersParams& params_;
  ClustersRendererPtr renderer_;
};

} // namespace Server
} // namespace Envoy
