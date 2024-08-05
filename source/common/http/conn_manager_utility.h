#pragma once

#include <atomic>
#include <cstdint>

#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/tracing/trace_reason.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http2/codec_stats.h"

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
                      headers_with_underscores_action,
                  Server::OverloadManager& overload_manager);

  /* The result after calling mutateRequestHeaders(), containing the final remote address. Note that
   * an extension used for detecting the original IP of the request might decide it should be
   * rejected if the detection failed. In this case, the reject_request optional will be set.
   */
  struct MutateRequestHeadersResult {
    Network::Address::InstanceConstSharedPtr final_remote_address;
    absl::optional<OriginalIPRejectRequestOptions> reject_request;
  };

  /**
   * Mutates request headers in various ways. This functionality is broken out because of its
   * complexity for ease of testing. See the method itself for detailed comments on what
   * mutations are performed.
   *
   * @return MutateRequestHeadersResult containing the final trusted remote address if detected.
   *         This depends on various settings and the existence of the x-forwarded-for header.
   *         Note that an extension might also be used. If detection fails, the result may contain
   *         options for rejecting the request.
   */
  static MutateRequestHeadersResult mutateRequestHeaders(RequestHeaderMap& request_headers,
                                                         Network::Connection& connection,
                                                         ConnectionManagerConfig& config,
                                                         const Router::Config& route_config,
                                                         const LocalInfo::LocalInfo& local_info,
                                                         const StreamInfo::StreamInfo& stream_info);
  /**
   * Mutates response headers in various ways. This functionality is broken out because of its
   * complexity for ease of testing. See the method itself for detailed comments on what
   * mutations are performed.
   *
   * Note this function may be called twice on the response path if there are
   * 100-Continue headers.
   *
   * @param response_headers the headers to mutate.
   * @param request_headers the request headers.
   * @param the configuration for the HCM, which affects request ID headers.
   * @param via the via header to append, if any.
   * @param stream_info a reference to the filter manager stream info.
   * @param proxy_name the proxy name.
   * @param clear_hop_by_hop_headers true if hop by hop headers should be
   *        cleared. This should only ever be false for envoy-mobile.
   */
  static void mutateResponseHeaders(ResponseHeaderMap& response_headers,
                                    const RequestHeaderMap* request_headers,
                                    ConnectionManagerConfig& config, const std::string& via,
                                    const StreamInfo::StreamInfo& stream_info,
                                    absl::string_view proxy_name,
                                    bool clear_hop_by_hop_headers = true);

  /**
   * Adds a Proxy-Status response header.
   *
   * Writing the Proxy-Status header is gated on the existence of
   * |proxy_status_config|. The |details| field and other internals are generated in
   * fromStreamInfo().
   * @param response_headers the headers to mutate.
   * @param the configuration for the HCM, which affects request ID headers.
   * @param stream_info a reference to the filter manager stream info.
   * @param proxy_name the proxy name.
   */
  static void setProxyStatusHeader(ResponseHeaderMap& response_headers,
                                   const ConnectionManagerConfig& config,
                                   const StreamInfo::StreamInfo& stream_info,
                                   absl::string_view proxy_name);

  enum class NormalizePathAction {
    Continue = 0,
    Reject = 1,
    Redirect = 2,
  };

  // Sanitize the path in the header map if the path exists and it is forced by config.
  // Side affect: the string view of Path header is invalidated.
  // Returns the action that should taken based on the results of path normalization.
  static NormalizePathAction maybeNormalizePath(RequestHeaderMap& request_headers,
                                                const ConnectionManagerConfig& config);

  static absl::optional<uint32_t> maybeNormalizeHost(RequestHeaderMap& request_headers,
                                                     const ConnectionManagerConfig& config,
                                                     uint32_t port);

  /**
   * Mutate request headers if request needs to be traced.
   * @return the trace reason selected after header mutation to be stored in stream info.
   */
  ABSL_MUST_USE_RESULT static Tracing::Reason
  mutateTracingRequestHeader(RequestHeaderMap& request_headers, Runtime::Loader& runtime,
                             ConnectionManagerConfig& config, const Router::Route* route);

private:
  static void appendXff(RequestHeaderMap& request_headers, Network::Connection& connection,
                        ConnectionManagerConfig& config);
  static void mutateXfccRequestHeader(RequestHeaderMap& request_headers,
                                      Network::Connection& connection,
                                      ConnectionManagerConfig& config);
  static void sanitizeTEHeader(RequestHeaderMap& request_headers);
  static void cleanInternalHeaders(RequestHeaderMap& request_headers, bool edge_request,
                                   const std::list<Http::LowerCaseString>& internal_only_headers);
};

} // namespace Http
} // namespace Envoy
