#pragma once

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_decoder.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_parser.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonRpc {

/**
 * Callback interface for streams created by JsonRpcConnectionManager.
 *
 * Mirrors the relationship between Http::ServerConnectionCallbacks and
 * Http::RequestDecoder: the connection manager calls newStream() once per
 * HTTP request, and the returned JsonRpcDecoder receives the parsed events.
 *
 * Implementors embed routing, policy, or transcoding logic here.
 */
class JsonRpcConnectionManagerCallbacks {
public:
  virtual ~JsonRpcConnectionManagerCallbacks() = default;

  /**
   * Called once per JSON-RPC capable HTTP request, before any body bytes arrive.
   *
   * Analogous to ServerConnectionCallbacks::newStream(): the connection manager
   * calls this to create the per-request decoder, then feeds it body events.
   *
   * @param request_headers    The request headers for this stream.
   * @param decoder_callbacks  The HTTP filter callbacks for this stream, forwarded
   *                           so the returned decoder can call sendLocalReply() if
   *                           the AI filter chain needs to short-circuit the request.
   * @return A JsonRpcDecoder that will receive onMethod/onId/onParams/… callbacks.
   *         The caller retains ownership; the returned reference must remain valid
   *         for the lifetime of the HTTP request.
   */
  virtual JsonRpcDecoder& newStream(const Http::RequestHeaderMap& request_headers,
                                    Http::StreamDecoderFilterCallbacks& decoder_callbacks) = 0;
};

/**
 * HTTP filter that translates the HTTP body into JSON-RPC structured events.
 *
 * Sits above the HTTP codec in the filter chain exactly as HCM sits above the
 * network codec:
 *
 *   TCP bytes → HTTP/1.1 codec → decodeHeaders / decodeData
 *   HTTP body → JsonRpcConnectionManager → onMethod / onId / onParams / …
 *
 * Lifecycle:
 *   1. decodeHeaders():  validates Content-Type, calls callbacks_.newStream()
 *                        to obtain a JsonRpcDecoder, creates a JsonRpcParser.
 *   2. decodeData():     feeds each body chunk to parser_.parse().
 *                        The parser fires decoder callbacks immediately for
 *                        scalars (onMethod, onId) and after accumulation for
 *                        nested values (onParams).
 *   3. end_stream=true:  calls parser_.finishParse() to validate completeness.
 *
 * The filter passes headers and data through unchanged; it is purely an
 * observer / demultiplexer, not a mutator.
 */
class JsonRpcConnectionManager : public Http::PassThroughDecoderFilter,
                                 public Logger::Loggable<Logger::Id::filter> {
public:
  explicit JsonRpcConnectionManager(JsonRpcConnectionManagerCallbacks& callbacks);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  // Send a JSON-RPC -32700 parse-error local reply (HTTP 400).
  void sendParseError();

  // Returns true if the request's Content-Type indicates a JSON-RPC payload.
  static bool isJsonRpcContentType(const Http::RequestHeaderMap& headers);

  JsonRpcConnectionManagerCallbacks& callbacks_;

  // Per-request decoder supplied by callbacks_.newStream().
  // Null until decodeHeaders() is called on a JSON-RPC request.
  JsonRpcDecoder* decoder_{nullptr};

  // Per-request streaming parser.
  // Created alongside decoder_ in decodeHeaders().
  std::unique_ptr<JsonRpcParser> parser_;
};

} // namespace JsonRpc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
