#pragma once

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/**
 * A utility to set admin health status from a specified host and health flag.
 *
 * @param healthFlag    The specific health status to be checked.
 * @param host          The target host.
 * @param health_status A proto reference representing the admin health status.
 */
void setHealthFlag(Upstream::Host::HealthFlag flag, const Upstream::Host& host,
                   envoy::admin::v3::HostHealthStatus& health_status);

class ClustersHandler : public HandlerContextBase {

public:
  ClustersHandler(Server::Instance& server);

  Http::Code handlerClusters(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
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
