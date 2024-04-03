#include "source/server/admin/clusters_request.h"

#include "envoy/server/instance.h"

namespace Envoy {
namespace Server {

ClustersRequest::ClustersRequest(Instance& server) : server_(server) {}

// TODO(demitriswan) Implement this member function.
Http::Code ClustersRequest::start(Http::ResponseHeaderMap& response_headers) {
  UNREFERENCED_PARAMETER(response_headers);
  return Http::Code::OK;
}

// TODO(demitriswan) Implement this member function.
bool ClustersRequest::nextChunk(Buffer::Instance& response) {
  UNREFERENCED_PARAMETER(response);
  UNREFERENCED_PARAMETER(chunk_size_);
  UNREFERENCED_PARAMETER(server_);
  return false;
}

} // namespace Server
} // namespace Envoy
