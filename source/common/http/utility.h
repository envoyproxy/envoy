#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/api/v2/core/protocol.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "common/json/json_loader.h"

namespace Envoy {
namespace Http {

/**
 * General HTTP utilities.
 */
class Utility {
public:
  typedef std::map<std::string, std::string> QueryParams;

  /**
   * Append to x-forwarded-for header.
   * @param headers supplies the headers to append to.
   * @param remote_address supplies the remote address to append.
   */
  static void appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address);

  /**
   * Creates an SSL (https) redirect path based on the input host and path headers.
   * @param headers supplies the request headers.
   * @return std::string the redirect path.
   */
  static std::string createSslRedirectPath(const HeaderMap& headers);

  /**
   * Parse a URL into query parameters.
   * @param url supplies the url to parse.
   * @return QueryParams the parsed parameters, if any.
   */
  static QueryParams parseQueryString(absl::string_view url);

  /**
   * Finds the start of the query string in a path
   * @param path supplies a HeaderString& to search for the query string
   * @return const char* a pointer to the beginning of the query string, or the end of the
   *         path if there is no query
   */
  static const char* findQueryStringStart(const HeaderString& path);

  /**
   * Parse a particular value out of a cookie
   * @param headers supplies the headers to get the cookie from.
   * @param key the key for the particular cookie value to return
   * @return std::string the parsed cookie value, or "" if none exists
   **/
  static std::string parseCookieValue(const HeaderMap& headers, const std::string& key);

  /**
   * Check whether a Set-Cookie header for the given cookie name exists
   * @param headers supplies the headers to search for the cookie
   * @param key the name of the cookie to search for
   * @return bool true if the cookie is set, false otherwise
   */
  static bool hasSetCookie(const HeaderMap& headers, const std::string& key);

  /**
   * Produce the value for a Set-Cookie header with the given parameters.
   * @param key is the name of the cookie that is being set.
   * @param value the value to set the cookie to; this value is trusted.
   * @param max_age the length of time for which the cookie is valid.
   * @return std::string a valid Set-Cookie header value string
   */
  static std::string makeSetCookieValue(const std::string& key, const std::string& value,
                                        const std::chrono::seconds max_age);

  /**
   * Get the response status from the response headers.
   * @param headers supplies the headers to get the status from.
   * @return uint64_t the response code or throws an exception if the headers are invalid.
   */
  static uint64_t getResponseStatus(const HeaderMap& headers);

  /**
   * Determine whether this is a WebSocket Upgrade request.
   * This function returns true if the following HTTP headers and values are present:
   * - Connection: Upgrade
   * - Upgrade: websocket
   */
  static bool isWebSocketUpgradeRequest(const HeaderMap& headers);

  /**
   * @param headers the headers to parse.
   * @return bool indicating whether content-type is gRPC.
   */
  static bool hasGrpcContentType(const HeaderMap& headers);

  /**
   * @param headers the headers to parse.
   * @param bool indicating wether the header is at end_stream.
   * @return bool indicating whether the header is a gRPC reseponse header
   */
  static bool isGrpcResponseHeader(const HeaderMap& headers, bool end_stream);

  /**
   * Returns the gRPC status code from a given HTTP response status code. Ordinarily, it is expected
   * that a 200 response is provided, but gRPC defines a mapping for intermediaries that are not
   * gRPC aware, see https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md.
   * @param http_response_status HTTP status code.
   * @return Status::GrpcStatus corresponding gRPC status code.
   */
  static Grpc::Status::GrpcStatus httpToGrpcStatus(uint64_t http_response_status);

  /**
   * @param grpc_status gRPC status from grpc-status header.
   * @return uint64_t the canonical HTTP status code corresponding to a gRPC status code.
   */
  static uint64_t grpcToHttpStatus(Grpc::Status::GrpcStatus grpc_status);

  /**
   * @return Http2Settings An Http2Settings populated from the
   * envoy::api::v2::core::Http2ProtocolOptions config.
   */
  static Http2Settings parseHttp2Settings(const envoy::api::v2::core::Http2ProtocolOptions& config);

  /**
   * @return Http1Settings An Http1Settings populated from the
   * envoy::api::v2::core::Http1ProtocolOptions config.
   */
  static Http1Settings parseHttp1Settings(const envoy::api::v2::core::Http1ProtocolOptions& config);

  /**
   * Create a locally generated response using filter callbacks.
   * @param is_grpc tells if this is a response to a gRPC request.
   * @param callbacks supplies the filter callbacks to use.
   * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
   *                 responsibility of the caller to ensure that this is set to false if onDestroy()
   *                 is invoked in the context of sendLocalReply().
   * @param response_code supplies the HTTP response code.
   * @param body_text supplies the optional body text which is sent using the text/plain content
   *                  type.
   */
  static void sendLocalReply(bool is_grpc, StreamDecoderFilterCallbacks& callbacks,
                             const bool& is_reset, Code response_code,
                             const std::string& body_text);

  /**
   * Create a locally generated response using the provided lambdas.
   * @param is_grpc tells if this is a response to a gRPC request.
   * @param encode_headers supplies the function to encode response headers.
   * @param encode_data supplies the function to encode the response body.
   * @param is_reset boolean reference that indicates whether a stream has been reset. It is the
   *                 responsibility of the caller to ensure that this is set to false if onDestroy()
   *                 is invoked in the context of sendLocalReply().
   * @param response_code supplies the HTTP response code.
   * @param body_text supplies the optional body text which is sent using the text/plain content
   *                  type.
   */
  static void
  sendLocalReply(bool is_grpc,
                 std::function<void(HeaderMapPtr&& headers, bool end_stream)> encode_headers,
                 std::function<void(Buffer::Instance& data, bool end_stream)> encode_data,
                 const bool& is_reset, Code response_code, const std::string& body_text);

  struct GetLastAddressFromXffInfo {
    // Last valid address pulled from the XFF header.
    Network::Address::InstanceConstSharedPtr address_;
    // Whether this is the only address in the XFF header.
    bool single_address_;
  };

  /**
   * Retrieves the last IPv4/IPv6 address in the x-forwarded-for header.
   * @param request_headers supplies the request headers.
   * @param num_to_skip specifies the number of addresses at the end of the XFF header
   *        to ignore when identifying the "last" address.
   * @return GetLastAddressFromXffInfo information about the last address in the XFF header.
   *         @see GetLastAddressFromXffInfo for more information.
   */
  static GetLastAddressFromXffInfo getLastAddressFromXFF(const Http::HeaderMap& request_headers,
                                                         uint32_t num_to_skip = 0);

  /**
   * Get the string for the given http protocol.
   * @param protocol for which to return the string representation.
   * @return string representation of the protocol.
   */
  static const std::string& getProtocolString(const Protocol p);

  /**
   * Appends data to header. If header already has a value, the string ',' is added between the
   * existing value and data.
   * @param header the header to append to.
   * @param data to append to the header.
   */
  static void appendToHeader(HeaderString& header, const std::string& data);
};

} // namespace Http
} // namespace Envoy
