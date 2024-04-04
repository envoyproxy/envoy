#include "source/server/admin/clusters_request.h"

#include <cstdint>
#include <format>
#include <memory>

#include "envoy/server/instance.h"

#include "source/common/common/logger.h"
#include "source/server/admin/clusters_renderer.h"

#include "clusters_params.h"

namespace Envoy {
namespace Server {

ClustersRequest::ClustersRequest(uint64_t chunk_limit, Instance& server,
                                 const ClustersParams& params)
    : chunk_limit_(chunk_limit), server_(server), params_(params) {}

Http::Code ClustersRequest::start(Http::ResponseHeaderMap& response_headers) {
  switch (params_.format_) {
  case ClustersParams::Format::Text:
    renderer_ = std::make_unique<ClustersTextRenderer>(
        chunk_limit_, response_headers, server_.clusterManager().clusters().active_clusters_);
    break;
  case ClustersParams::Format::Json:
    renderer_ = std::make_unique<ClustersTextRenderer>(
        chunk_limit_, response_headers, server_.clusterManager().clusters().active_clusters_);
    break;
  default:
    // TODO(demitriswan) handle this case properly.
    return Http::Code::BadRequest;
  }
  return Http::Code::OK;
}

bool ClustersRequest::nextChunk(Buffer::Instance& response) {
  return renderer_->nextChunk(response);
}

} // namespace Server
} // namespace Envoy
