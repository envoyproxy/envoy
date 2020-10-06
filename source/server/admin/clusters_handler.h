#pragma once

#include "envoy/buffer/buffer.h"
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
                             Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                             AdminStream&);

private:
  void addOutlierInfo(const std::string& cluster_name,
                      const Upstream::Outlier::Detector* outlier_detector,
                      Buffer::Instance& response);
  void writeClustersAsJson(Buffer::Instance& response);
  void writeClustersAsText(Buffer::Instance& response);
};

} // namespace Server
} // namespace Envoy
