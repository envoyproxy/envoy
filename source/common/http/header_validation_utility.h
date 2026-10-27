#pragma once

#include <optional>

#include "envoy/http/codec.h"
#include "envoy/http/header_validator.h"
#include "envoy/http/protocol.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace HeaderValidationUtility {

#ifdef ENVOY_ENABLE_UHV

/**
 * Validates and transforms (in place) fully parsed request headers using the supplied server
 * header validator, and translates the validator's verdict into the decision the request decoder
 * must act on. Shared by the HTTP/1, HTTP/2 and HTTP/3 server codecs so that the three protocols
 * keep identical rejection semantics.
 *
 * @param validator the per-stream server header validator.
 * @param headers the request headers; transformed in place when accepted (and possibly partially
 *        transformed when rejected).
 * @param protocol the protocol of the connection (drives protocol specific quirks).
 * @param stream_error_on_invalid_http_message the codec's stream_error_on_invalid_http_message
 *        setting.
 * @return std::nullopt when the headers were accepted; otherwise a RequestRejection with
 *         `phase == Phase::Headers` and all decision fields populated. The caller is responsible
 *         for moving the headers (and end_stream) into the returned RequestRejection before
 *         passing it to RequestDecoder::rejectRequest().
 */
std::optional<RequestRejection> checkRequestHeaders(ServerHeaderValidator& validator,
                                                    RequestHeaderMap& headers, Protocol protocol,
                                                    bool stream_error_on_invalid_http_message);

/**
 * Validates and transforms (in place) fully parsed request trailers using the supplied server
 * header validator. See checkRequestHeaders() for the parameters.
 *
 * @return std::nullopt when the trailers were accepted; otherwise a RequestRejection with
 *         `phase == Phase::Trailers` and all decision fields populated. The caller is responsible
 *         for moving the trailers into the returned RequestRejection before passing it to
 *         RequestDecoder::rejectRequest().
 */
std::optional<RequestRejection> checkRequestTrailers(ServerHeaderValidator& validator,
                                                     RequestTrailerMap& trailers, Protocol protocol,
                                                     bool stream_error_on_invalid_http_message);

/**
 * @return true for header validation failures that historically (pre UHV) were always answered
 *         with a local reply and never closed the connection, regardless of
 *         stream_error_on_invalid_http_message.
 */
bool streamErrorOnlyErrors(absl::string_view error_details);

#endif // ENVOY_ENABLE_UHV

} // namespace HeaderValidationUtility
} // namespace Http
} // namespace Envoy
