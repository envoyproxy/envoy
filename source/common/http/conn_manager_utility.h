#pragma once

#include <atomic>
#include <cstdint>

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/http/conn_manager_impl.h"
#include "common/http/http1/codec_stats.h"
#include "common/http/http2/codec_stats.h"

namespace Envoy {
namespace Http {

/**
 * Connection manager utilities split out for ease of testing.
 */
class ConnectionManagerUtility {
public:
  /**
   * Determine the next protocol to used based both on ALPN as well as protocol inspection.
   * @param connection supplies the connection to determine a protocol for.
   * @param data supplies the currently available read data on the connection.
   */
  static std::string determineNextProtocol(Network::Connection& connection,
                                           const Buffer::Instance& data);

  /**
   * Create an HTTP codec given the connection and the beginning of the incoming data.
   * @param connection supplies the connection.
   * @param data supplies the initial data supplied by the client.
   * @param callbacks supplies the codec callbacks.
   * @param scope supplies the stats scope for codec stats.
   * @param http1_settings supplies the HTTP/1 settings to use if HTTP/1 is chosen.
   * @param http2_settings supplies the HTTP/2 settings to use if HTTP/2 is chosen.
   */
  static ServerConnectionPtr
  autoCreateCodec(Network::Connection& connection, const Buffer::Instance& data,
                  ServerConnectionCallbacks& callbacks, Stats::Scope& scope,
                  Random::RandomGenerator& random, Http1::CodecStats::AtomicPtr& http1_codec_stats,
                  Http2::CodecStats::AtomicPtr& http2_codec_stats,
                  const Http1Settings& http1_settings,
                  const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                  uint32_t max_request_headers_kb, uint32_t max_request_headers_count,
                  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
                      headers_with_underscores_action);

  /**
   * Mutates request headers in various ways. This functionality is broken out because of its
   * complexity for ease of testing. See the method itself for detailed comments on what
   * mutations are performed.
   *
   * Note this function may be called twice on the response path if there are
   * 100-Continue headers.
   *
   * @return the final trusted remote address. This depends on various settings and the
   *         existence of the x-forwarded-for header. Again see the method for more details.
   */
  static Network::Address::InstanceConstSharedPtr
  mutateRequestHeaders(RequestHeaderMap& request_headers, Network::Connection& connection,
                       ConnectionManagerConfig& config, const Router::Config& route_config,
                       const LocalInfo::LocalInfo& local_info);

  static void mutateResponseHeaders(ResponseHeaderMap& response_headers,
                                    const RequestHeaderMap* request_headers,
                                    ConnectionManagerConfig& config, const std::string& via);

  // Sanitize the path in the header map if the path exists and it is forced by config.
  // Side affect: the string view of Path header is invalidated.
  // Return false if error happens during the sanitization.
  // Returns true if there is no path.
  static bool maybeNormalizePath(RequestHeaderMap& request_headers,
                                 const ConnectionManagerConfig& config);

  static void maybeNormalizeHost(RequestHeaderMap& request_headers,
                                 const ConnectionManagerConfig& config, uint32_t port);

  /**
   * Mutate request headers if request needs to be traced.
   */
  static void mutateTracingRequestHeader(RequestHeaderMap& request_headers,
                                         Runtime::Loader& runtime, ConnectionManagerConfig& config,
                                         const Router::Route* route);

private:
  static void mutateXfccRequestHeader(RequestHeaderMap& request_headers,
                                      Network::Connection& connection,
                                      ConnectionManagerConfig& config);
};

} // namespace Http
} // namespace Envoy
