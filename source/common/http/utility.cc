#include "common/http/utility.h"

#include <http_parser.h>

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/grpc/status.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

static const char kDefaultPath[] = "/";

bool Utility::Url::initialize(absl::string_view absolute_url) {
  struct http_parser_url u;
  const bool is_connect = false;
  http_parser_url_init(&u);
  const int result =
      http_parser_parse_url(absolute_url.data(), absolute_url.length(), is_connect, &u);

  if (result != 0) {
    return false;
  }
  if ((u.field_set & (1 << UF_HOST)) != (1 << UF_HOST) &&
      (u.field_set & (1 << UF_SCHEMA)) != (1 << UF_SCHEMA)) {
    return false;
  }
  scheme_ = absl::string_view(absolute_url.data() + u.field_data[UF_SCHEMA].off,
                              u.field_data[UF_SCHEMA].len);

  uint16_t authority_len = u.field_data[UF_HOST].len;
  if ((u.field_set & (1 << UF_PORT)) == (1 << UF_PORT)) {
    authority_len = authority_len + u.field_data[UF_PORT].len + 1;
  }
  host_and_port_ =
      absl::string_view(absolute_url.data() + u.field_data[UF_HOST].off, authority_len);

  // RFC allows the absolute-uri to not end in /, but the absolute path form
  // must start with
  uint64_t path_len =
      absolute_url.length() - (u.field_data[UF_HOST].off + host_and_port().length());
  if (path_len > 0) {
    uint64_t path_beginning = u.field_data[UF_HOST].off + host_and_port().length();
    path_and_query_params_ = absl::string_view(absolute_url.data() + path_beginning, path_len);
  } else {
    path_and_query_params_ = absl::string_view(kDefaultPath, 1);
  }
  return true;
}

void Utility::appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address) {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return;
  }

  HeaderString& header = headers.insertForwardedFor().value();
  const std::string& address_as_string = remote_address.ip()->addressAsString();
  HeaderMapImpl::appendToHeader(header, address_as_string.c_str());
}

void Utility::appendVia(HeaderMap& headers, const std::string& via) {
  HeaderString& header = headers.insertVia().value();
  if (!header.empty()) {
    header.append(", ", 2);
  }
  header.append(via.c_str(), via.size());
}

std::string Utility::createSslRedirectPath(const HeaderMap& headers) {
  ASSERT(headers.Host());
  ASSERT(headers.Path());
  return fmt::format("https://{}{}", headers.Host()->value().getStringView(),
                     headers.Path()->value().getStringView());
}

Utility::QueryParams Utility::parseQueryString(absl::string_view url) {
  size_t start = url.find('?');
  if (start == std::string::npos) {
    QueryParams params;
    return params;
  }

  start++;
  return parseParameters(url, start);
}

Utility::QueryParams Utility::parseFromBody(absl::string_view body) {
  return parseParameters(body, 0);
}

Utility::QueryParams Utility::parseParameters(absl::string_view data, size_t start) {
  QueryParams params;

  while (start < data.size()) {
    size_t end = data.find('&', start);
    if (end == std::string::npos) {
      end = data.size();
    }
    absl::string_view param(data.data() + start, end - start);

    const size_t equal = param.find('=');
    if (equal != std::string::npos) {
      params.emplace(StringUtil::subspan(data, start, start + equal),
                     StringUtil::subspan(data, start + equal + 1, end));
    } else {
      params.emplace(StringUtil::subspan(data, start, end), "");
    }

    start = end + 1;
  }

  return params;
}

absl::string_view Utility::findQueryStringStart(const HeaderString& path) {
  absl::string_view path_str = path.getStringView();
  size_t query_offset = path_str.find('?');
  if (query_offset == absl::string_view::npos) {
    query_offset = path_str.length();
  }
  path_str.remove_prefix(query_offset);
  return path_str;
}

std::string Utility::parseCookieValue(const HeaderMap& headers, const std::string& key) {

  struct State {
    std::string key_;
    std::string ret_;
  };

  State state;
  state.key_ = key;

  headers.iterateReverse(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // Find the cookie headers in the request (typically, there's only one).
        if (header.key() == Http::Headers::get().Cookie.get()) {

          // Split the cookie header into individual cookies.
          for (const auto s : StringUtil::splitToken(header.value().getStringView(), ";")) {
            // Find the key part of the cookie (i.e. the name of the cookie).
            size_t first_non_space = s.find_first_not_of(" ");
            size_t equals_index = s.find('=');
            if (equals_index == absl::string_view::npos) {
              // The cookie is malformed if it does not have an `=`. Continue
              // checking other cookies in this header.
              continue;
            }
            const absl::string_view k = s.substr(first_non_space, equals_index - first_non_space);
            State* state = static_cast<State*>(context);
            // If the key matches, parse the value from the rest of the cookie string.
            if (k == state->key_) {
              absl::string_view v = s.substr(equals_index + 1, s.size() - 1);

              // Cookie values may be wrapped in double quotes.
              // https://tools.ietf.org/html/rfc6265#section-4.1.1
              if (v.size() >= 2 && v.back() == '"' && v[0] == '"') {
                v = v.substr(1, v.size() - 2);
              }
              state->ret_ = std::string{v};
              return HeaderMap::Iterate::Break;
            }
          }
        }
        return HeaderMap::Iterate::Continue;
      },
      &state);

  return state.ret_;
}

std::string Utility::makeSetCookieValue(const std::string& key, const std::string& value,
                                        const std::string& path, const std::chrono::seconds max_age,
                                        bool httponly) {
  std::string cookie_value;
  // Best effort attempt to avoid numerous string copies.
  cookie_value.reserve(value.size() + path.size() + 30);

  cookie_value = absl::StrCat(key, "=\"", value, "\"");
  if (max_age != std::chrono::seconds::zero()) {
    absl::StrAppend(&cookie_value, "; Max-Age=", max_age.count());
  }
  if (!path.empty()) {
    absl::StrAppend(&cookie_value, "; Path=", path);
  }
  if (httponly) {
    absl::StrAppend(&cookie_value, "; HttpOnly");
  }
  return cookie_value;
}

uint64_t Utility::getResponseStatus(const HeaderMap& headers) {
  const HeaderEntry* header = headers.Status();
  uint64_t response_code;
  if (!header || !absl::SimpleAtoi(headers.Status()->value().getStringView(), &response_code)) {
    throw CodecClientException(":status must be specified and a valid unsigned long");
  }
  return response_code;
}

bool Utility::isUpgrade(const HeaderMap& headers) {
  // In firefox the "Connection" request header value is "keep-alive, Upgrade",
  // we should check if it contains the "Upgrade" token.
  return (headers.Connection() && headers.Upgrade() &&
          Envoy::StringUtil::caseFindToken(headers.Connection()->value().getStringView(), ",",
                                           Http::Headers::get().ConnectionValues.Upgrade.c_str()));
}

bool Utility::isH2UpgradeRequest(const HeaderMap& headers) {
  return headers.Method() &&
         headers.Method()->value().getStringView() == Http::Headers::get().MethodValues.Connect &&
         headers.Protocol() && !headers.Protocol()->value().empty();
}

bool Utility::isWebSocketUpgradeRequest(const HeaderMap& headers) {
  return (isUpgrade(headers) &&
          absl::EqualsIgnoreCase(headers.Upgrade()->value().getStringView(),
                                 Http::Headers::get().UpgradeValues.WebSocket));
}

Http2Settings
Utility::parseHttp2Settings(const envoy::api::v2::core::Http2ProtocolOptions& config) {
  Http2Settings ret;
  ret.hpack_table_size_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, hpack_table_size, Http::Http2Settings::DEFAULT_HPACK_TABLE_SIZE);
  ret.max_concurrent_streams_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, max_concurrent_streams, Http::Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS);
  ret.initial_stream_window_size_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, initial_stream_window_size, Http::Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  ret.initial_connection_window_size_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, initial_connection_window_size,
                                      Http::Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
  ret.max_outbound_frames_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, max_outbound_frames, Http::Http2Settings::DEFAULT_MAX_OUTBOUND_FRAMES);
  ret.max_outbound_control_frames_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_outbound_control_frames,
                                      Http::Http2Settings::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES);
  ret.max_consecutive_inbound_frames_with_empty_payload_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, max_consecutive_inbound_frames_with_empty_payload,
      Http::Http2Settings::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD);
  ret.max_inbound_priority_frames_per_stream_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, max_inbound_priority_frames_per_stream,
      Http::Http2Settings::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM);
  ret.max_inbound_window_update_frames_per_data_frame_sent_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config, max_inbound_window_update_frames_per_data_frame_sent,
      Http::Http2Settings::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT);
  ret.allow_connect_ = config.allow_connect();
  ret.allow_metadata_ = config.allow_metadata();
  ret.stream_error_on_invalid_http_messaging_ = config.stream_error_on_invalid_http_messaging();
  return ret;
}

Http1Settings
Utility::parseHttp1Settings(const envoy::api::v2::core::Http1ProtocolOptions& config) {
  Http1Settings ret;
  ret.allow_absolute_url_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, allow_absolute_url, true);
  ret.accept_http_10_ = config.accept_http_10();
  ret.default_host_for_http_10_ = config.default_host_for_http_10();

  if (config.header_key_format().has_proper_case_words()) {
    ret.header_key_format_ = Http1Settings::HeaderKeyFormat::ProperCase;
  } else {
    ret.header_key_format_ = Http1Settings::HeaderKeyFormat::Default;
  }

  return ret;
}

void Utility::sendLocalReply(bool is_grpc, StreamDecoderFilterCallbacks& callbacks,
                             const bool& is_reset, Code response_code, absl::string_view body_text,
                             const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                             bool is_head_request) {
  sendLocalReply(
      is_grpc,
      [&](HeaderMapPtr&& headers, bool end_stream) -> void {
        callbacks.encodeHeaders(std::move(headers), end_stream);
      },
      [&](Buffer::Instance& data, bool end_stream) -> void {
        callbacks.encodeData(data, end_stream);
      },
      is_reset, response_code, body_text, grpc_status, is_head_request);
}

void Utility::sendLocalReply(
    bool is_grpc, std::function<void(HeaderMapPtr&& headers, bool end_stream)> encode_headers,
    std::function<void(Buffer::Instance& data, bool end_stream)> encode_data, const bool& is_reset,
    Code response_code, absl::string_view body_text,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, bool is_head_request) {
  // encode_headers() may reset the stream, so the stream must not be reset before calling it.
  ASSERT(!is_reset);
  // Respond with a gRPC trailers-only response if the request is gRPC
  if (is_grpc) {
    HeaderMapPtr response_headers{new HeaderMapImpl{
        {Headers::get().Status, std::to_string(enumToInt(Code::OK))},
        {Headers::get().ContentType, Headers::get().ContentTypeValues.Grpc},
        {Headers::get().GrpcStatus,
         std::to_string(
             enumToInt(grpc_status ? grpc_status.value()
                                   : Grpc::Utility::httpToGrpcStatus(enumToInt(response_code))))}}};
    if (!body_text.empty() && !is_head_request) {
      // TODO(dio): Probably it is worth to consider caching the encoded message based on gRPC
      // status.
      response_headers->insertGrpcMessage().value(PercentEncoding::encode(body_text));
    }
    encode_headers(std::move(response_headers), true); // Trailers only response
    return;
  }

  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(response_code))}}};
  if (!body_text.empty()) {
    response_headers->insertContentLength().value(body_text.size());
    response_headers->insertContentType().value(Headers::get().ContentTypeValues.Text);
  }

  if (is_head_request) {
    encode_headers(std::move(response_headers), true);
    return;
  }

  encode_headers(std::move(response_headers), body_text.empty());
  // encode_headers()) may have changed the referenced is_reset so we need to test it
  if (!body_text.empty() && !is_reset) {
    Buffer::OwnedImpl buffer(body_text);
    encode_data(buffer, true);
  }
}

Utility::GetLastAddressFromXffInfo
Utility::getLastAddressFromXFF(const Http::HeaderMap& request_headers, uint32_t num_to_skip) {
  const auto xff_header = request_headers.ForwardedFor();
  if (xff_header == nullptr) {
    return {nullptr, false};
  }

  absl::string_view xff_string(xff_header->value().getStringView());
  static const std::string separator(",");
  // Ignore the last num_to_skip addresses at the end of XFF.
  for (uint32_t i = 0; i < num_to_skip; i++) {
    std::string::size_type last_comma = xff_string.rfind(separator);
    if (last_comma == std::string::npos) {
      return {nullptr, false};
    }
    xff_string = xff_string.substr(0, last_comma);
  }
  // The text after the last remaining comma, or the entirety of the string if there
  // is no comma, is the requested IP address.
  std::string::size_type last_comma = xff_string.rfind(separator);
  if (last_comma != std::string::npos && last_comma + separator.size() < xff_string.size()) {
    xff_string = xff_string.substr(last_comma + separator.size());
  }

  // Ignore the whitespace, since they are allowed in HTTP lists (see RFC7239#section-7.1).
  xff_string = StringUtil::ltrim(xff_string);
  xff_string = StringUtil::rtrim(xff_string);

  try {
    // This technically requires a copy because inet_pton takes a null terminated string. In
    // practice, we are working with a view at the end of the owning string, and could pass the
    // raw pointer.
    // TODO(mattklein123) PERF: Avoid the copy here.
    return {
        Network::Utility::parseInternetAddress(std::string(xff_string.data(), xff_string.size())),
        last_comma == std::string::npos && num_to_skip == 0};
  } catch (const EnvoyException&) {
    return {nullptr, false};
  }
}

const std::string& Utility::getProtocolString(const Protocol protocol) {
  switch (protocol) {
  case Protocol::Http10:
    return Headers::get().ProtocolStrings.Http10String;
  case Protocol::Http11:
    return Headers::get().ProtocolStrings.Http11String;
  case Protocol::Http2:
    return Headers::get().ProtocolStrings.Http2String;
  case Protocol::Http3:
    return Headers::get().ProtocolStrings.Http3String;
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void Utility::extractHostPathFromUri(const absl::string_view& uri, absl::string_view& host,
                                     absl::string_view& path) {
  /**
   *  URI RFC: https://www.ietf.org/rfc/rfc2396.txt
   *
   *  Example:
   *  uri  = "https://example.com:8443/certs"
   *  pos:         ^
   *  host_pos:       ^
   *  path_pos:                       ^
   *  host = "example.com:8443"
   *  path = "/certs"
   */
  const auto pos = uri.find("://");
  // Start position of the host
  const auto host_pos = (pos == std::string::npos) ? 0 : pos + 3;
  // Start position of the path
  const auto path_pos = uri.find("/", host_pos);
  if (path_pos == std::string::npos) {
    // If uri doesn't have "/", the whole string is treated as host.
    host = uri.substr(host_pos);
    path = "/";
  } else {
    host = uri.substr(host_pos, path_pos - host_pos);
    path = uri.substr(path_pos);
  }
}

MessagePtr Utility::prepareHeaders(const ::envoy::api::v2::core::HttpUri& http_uri) {
  absl::string_view host, path;
  extractHostPathFromUri(http_uri.uri(), host, path);

  MessagePtr message(new RequestMessageImpl());
  message->headers().insertPath().value(path.data(), path.size());
  message->headers().insertHost().value(host.data(), host.size());

  return message;
}

// TODO(jmarantz): make QueryParams a real class and put this serializer there,
// along with proper URL escaping of the name and value.
std::string Utility::queryParamsToString(const QueryParams& params) {
  std::string out;
  std::string delim = "?";
  for (const auto& p : params) {
    absl::StrAppend(&out, delim, p.first, "=", p.second);
    delim = "&";
  }
  return out;
}

const std::string Utility::resetReasonToString(const Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::ConnectionFailure:
    return "connection failure";
  case Http::StreamResetReason::ConnectionTermination:
    return "connection termination";
  case Http::StreamResetReason::LocalReset:
    return "local reset";
  case Http::StreamResetReason::LocalRefusedStreamReset:
    return "local refused stream reset";
  case Http::StreamResetReason::Overflow:
    return "overflow";
  case Http::StreamResetReason::RemoteReset:
    return "remote reset";
  case Http::StreamResetReason::RemoteRefusedStreamReset:
    return "remote refused stream reset";
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void Utility::transformUpgradeRequestFromH1toH2(HeaderMap& headers) {
  ASSERT(Utility::isUpgrade(headers));

  const HeaderString& upgrade = headers.Upgrade()->value();
  headers.insertMethod().value().setReference(Http::Headers::get().MethodValues.Connect);
  headers.insertProtocol().value().setCopy(upgrade.getStringView());
  headers.removeUpgrade();
  headers.removeConnection();
  // nghttp2 rejects upgrade requests/responses with content length, so strip
  // any unnecessary content length header.
  if (headers.ContentLength() != nullptr &&
      headers.ContentLength()->value().getStringView() == "0") {
    headers.removeContentLength();
  }
}

void Utility::transformUpgradeResponseFromH1toH2(HeaderMap& headers) {
  if (getResponseStatus(headers) == 101) {
    headers.insertStatus().value().setInteger(200);
  }
  headers.removeUpgrade();
  headers.removeConnection();
  if (headers.ContentLength() != nullptr &&
      headers.ContentLength()->value().getStringView() == "0") {
    headers.removeContentLength();
  }
}

void Utility::transformUpgradeRequestFromH2toH1(HeaderMap& headers) {
  ASSERT(Utility::isH2UpgradeRequest(headers));

  const HeaderString& protocol = headers.Protocol()->value();
  headers.insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  headers.insertUpgrade().value().setCopy(protocol.getStringView());
  headers.insertConnection().value().setReference(Http::Headers::get().ConnectionValues.Upgrade);
  headers.removeProtocol();
}

void Utility::transformUpgradeResponseFromH2toH1(HeaderMap& headers, absl::string_view upgrade) {
  if (getResponseStatus(headers) == 200) {
    headers.insertUpgrade().value().setCopy(upgrade);
    headers.insertConnection().value().setReference(Http::Headers::get().ConnectionValues.Upgrade);
    headers.insertStatus().value().setInteger(101);
  }
}

const Router::RouteSpecificFilterConfig*
Utility::resolveMostSpecificPerFilterConfigGeneric(const std::string& filter_name,
                                                   const Router::RouteConstSharedPtr& route) {

  const Router::RouteSpecificFilterConfig* maybe_filter_config{};
  traversePerFilterConfigGeneric(
      filter_name, route, [&maybe_filter_config](const Router::RouteSpecificFilterConfig& cfg) {
        maybe_filter_config = &cfg;
      });
  return maybe_filter_config;
}

void Utility::traversePerFilterConfigGeneric(
    const std::string& filter_name, const Router::RouteConstSharedPtr& route,
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb) {
  if (!route) {
    return;
  }

  const Router::RouteEntry* routeEntry = route->routeEntry();

  if (routeEntry != nullptr) {
    auto maybe_vhost_config = routeEntry->virtualHost().perFilterConfig(filter_name);
    if (maybe_vhost_config != nullptr) {
      cb(*maybe_vhost_config);
    }
  }

  auto maybe_route_config = route->perFilterConfig(filter_name);
  if (maybe_route_config != nullptr) {
    cb(*maybe_route_config);
  }

  if (routeEntry != nullptr) {
    auto maybe_weighted_cluster_config = routeEntry->perFilterConfig(filter_name);
    if (maybe_weighted_cluster_config != nullptr) {
      cb(*maybe_weighted_cluster_config);
    }
  }
}

std::string Utility::PercentEncoding::encode(absl::string_view value) {
  for (size_t i = 0; i < value.size(); ++i) {
    const char& ch = value[i];
    // The escaping characters are defined in
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses.
    //
    // We do checking for each char in the string. If the current char is included in the defined
    // escaping characters, we jump to "the slow path" (append the char [encoded or not encoded] to
    // the returned string one by one) started from the current index.
    if (ch < ' ' || ch >= '~' || ch == '%') {
      return PercentEncoding::encode(value, i);
    }
  }
  return std::string(value);
}

std::string Utility::PercentEncoding::encode(absl::string_view value, const size_t index) {
  std::string encoded;
  if (index > 0) {
    absl::StrAppend(&encoded, value.substr(0, index - 1));
  }

  for (size_t i = index; i < value.size(); ++i) {
    const char& ch = value[i];
    if (ch < ' ' || ch >= '~' || ch == '%') {
      // For consistency, URI producers should use uppercase hexadecimal digits for all
      // percent-encodings. https://tools.ietf.org/html/rfc3986#section-2.1.
      absl::StrAppend(&encoded, fmt::format("%{:02X}", ch));
    } else {
      encoded.push_back(ch);
    }
  }
  return encoded;
}

std::string Utility::PercentEncoding::decode(absl::string_view encoded) {
  std::string decoded;
  decoded.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i) {
    char ch = encoded[i];
    if (ch == '%' && i + 2 < encoded.size()) {
      const char& hi = encoded[i + 1];
      const char& lo = encoded[i + 2];
      if (absl::ascii_isdigit(hi)) {
        ch = hi - '0';
      } else {
        ch = absl::ascii_toupper(hi) - 'A' + 10;
      }

      ch *= 16;
      if (absl::ascii_isdigit(lo)) {
        ch += lo - '0';
      } else {
        ch += absl::ascii_toupper(lo) - 'A' + 10;
      }
      i += 2;
    }
    decoded.push_back(ch);
  }
  return decoded;
}

} // namespace Http
} // namespace Envoy
