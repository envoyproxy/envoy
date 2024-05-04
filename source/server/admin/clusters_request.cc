#include "source/server/admin/clusters_request.h"

#include <cstdint>
#include <memory>

#include "envoy/server/instance.h"

#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "source/server/admin/clusters_chunk_processor.h"

#include "clusters_params.h"

namespace Envoy {
namespace Server {

ClustersRequest::ClustersRequest(uint64_t chunk_limit, Instance& server,
                                 const ClustersParams& params)
    : chunk_limit_(chunk_limit), server_(server), params_(params) {}

Http::Code ClustersRequest::start(Http::ResponseHeaderMap& response_headers) {
  switch (params_.format_) {
  case ClustersParams::Format::Text:
    chunk_processor_ = std::make_unique<TextClustersChunkProcessor>(
        chunk_limit_, response_headers, server_.clusterManager().clusters().active_clusters_);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
    break;
  case ClustersParams::Format::Json:
    chunk_processor_ = std::make_unique<JsonClustersChunkProcessor>(
        chunk_limit_, response_headers, server_.clusterManager().clusters().active_clusters_);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    break;
  default:
    // TODO(demitriswan) handle this case properly.
    return Http::Code::BadRequest;
  }
  return Http::Code::OK;
}

bool ClustersRequest::nextChunk(Buffer::Instance& response) {
  return chunk_processor_->nextChunk(response);
}

} // namespace Server
} // namespace Envoy
