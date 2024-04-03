#include "source/server/admin/clusters_request.h"

#include <cstdint>
#include <memory>

#include "envoy/server/instance.h"

#include "source/server/admin/clusters_renderer.h"

#include "clusters_params.h"

namespace Envoy {
namespace Server {

ClustersRequest::ClustersRequest(uint64_t chunk_limit, Instance& server, Buffer::Instance& response,
                                 const ClustersParams& params)
    : chunk_limit_(chunk_limit), server_(server), response_(response), params_(params) {}

Http::Code ClustersRequest::start(Http::ResponseHeaderMap& response_headers) {
  switch (params_.format_) {
  case ClustersParams::Format::Text:
    renderer_ = std::make_unique<ClustersTextRenderer>(
        chunk_limit_, response_headers, response_,
        server_.clusterManager().clusters().active_clusters_);
    break;
  case ClustersParams::Format::Json:
    renderer_ = std::make_unique<ClustersTextRenderer>(
        chunk_limit_, response_headers, response_,
        server_.clusterManager().clusters().active_clusters_);
    break;
  default:
    // TODO(demitriswan) handle this case properly.
    return Http::Code::BadRequest;
  }
  return Http::Code::OK;
}

bool ClustersRequest::nextChunk(Buffer::Instance& response) {
  // When reading the documentation for Request, I noted that it was
  // a mistake to add the buffer to the call to nextChunk since we
  // should always process the same buffer on each call. So, in this
  // implementation I ignore the parameter here and use the buffer
  // associated with this request from cosntruction.
  UNREFERENCED_PARAMETER(response);
  return renderer_->nextChunk();
}

} // namespace Server
} // namespace Envoy
