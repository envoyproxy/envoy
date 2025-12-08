#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class LoadShedPointNameValues {
public:
  // Envoy will reject (close) new TCP connections.
  // This occurs before the Listener Filter Chain is created.
  const std::string TcpListenerAccept = "envoy.load_shed_points.tcp_listener_accept";

  // Envoy will reject new HTTP streams by sending a local reply.
  const std::string HcmDecodeHeaders =
      "envoy.load_shed_points.http_connection_manager_decode_headers";

  // Envoy will reject processing HTTP1 at the codec level.
  const std::string H1ServerAbortDispatch = "envoy.load_shed_points.http1_server_abort_dispatch";

  // Envoy will send a GOAWAY while processing HTTP2 requests at the codec level
  // which will eventually drain the HTTP/2 connection.
  const std::string H2ServerGoAwayOnDispatch =
      "envoy.load_shed_points.http2_server_go_away_on_dispatch";

  // Envoy will send a GOAWAY and immediately close the connection while processing HTTP2 requests
  // at the codec level.
  const std::string H2ServerGoAwayAndCloseOnDispatch =
      "envoy.load_shed_points.http2_server_go_away_and_close_on_dispatch";

  // Envoy will close the connections before creating codec if Envoy is under pressure,
  // typically memory. This happens once geting data from the connection.
  const std::string HcmCodecCreation = "envoy.load_shed_points.hcm_ondata_creating_codec";

  const std::string HttpDownstreamFilterCheck =
      "envoy.load_shed_points.http_downstream_filter_check";

  // Envoy will stop creating new connections in the connection pool when
  // it is under pressure (typically memory pressure). If a new connection is
  // rejected by this load shed point and there is no available capacity
  // to serve the downstream request, the downstream request will fail.
  const std::string ConnectionPoolNewConnection =
      "envoy.load_shed_points.connection_pool_new_connection";

  // Envoy will send GOAWAY and immediately close the connection while
  // processing HTTP3 requests at the codec level.
  const std::string H3ServerGoAwayAndCloseOnDispatch =
      "envoy.load_shed_points.http3_server_go_away_and_close_on_dispatch";

  // Envoy will send a GOAWAY while processing HTTP3 requests at the codec level
  // which will eventually drain the HPPT/3 connection.
  const std::string H3ServerGoAwayOnDispatch =
      "envoy.load_shed_points.http3_server_go_away_on_dispatch";
};

using LoadShedPointName = ConstSingleton<LoadShedPointNameValues>;

/**
 * A point within the connection or request lifecycle that provides context on
 * whether to shed load at that given stage for the current entity at the point.
 */
class LoadShedPoint {
public:
  virtual ~LoadShedPoint() = default;

  // Whether to shed the load.
  virtual bool shouldShedLoad() PURE;
};

using LoadShedPointPtr = std::unique_ptr<LoadShedPoint>;

/**
 * Provides configured LoadShedPoints.
 */
class LoadShedPointProvider {
public:
  virtual ~LoadShedPointProvider() = default;

  /**
   * Get the load shed point identified by the following string. Returns nullptr
   * for non-configured points.
   */
  virtual LoadShedPoint* getLoadShedPoint(absl::string_view point_name) PURE;
};

} // namespace Server
} // namespace Envoy
