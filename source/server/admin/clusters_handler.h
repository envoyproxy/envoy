#pragma once

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ClustersHandler : public HandlerContextBase {

public:
  ClustersHandler(Server::Instance& server);

  Http::Code handlerClusters(absl::string_view path_and_query,
                             Http::ResponseHeaderMap& response_headers, Buffer::Chunker& response,
                             AdminStream&);

private:
  void addOutlierInfo(const std::string& cluster_name,
                      const Upstream::Outlier::Detector* outlier_detector,
                      Buffer::Chunker& response);
  void writeClustersAsJson(Buffer::Chunker& response);
  void writeClustersAsText(Buffer::Chunker& response);
};

} // namespace Server
} // namespace Envoy
