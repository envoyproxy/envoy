#include "common/http/utility.h"

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

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

void Utility::appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address) {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return;
  }

  // TODO(alyssawilk) move over to the append utility.
  HeaderString& header = headers.insertForwardedFor().value();
  if (!header.empty()) {
    header.append(", ", 2);
  }

  const std::string& address_as_string = remote_address.ip()->addressAsString();
  header.append(address_as_string.c_str(), address_as_string.size());
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
  return fmt::format("https://{}{}", headers.Host()->value().c_str(),
                     headers.Path()->value().c_str());
}

Utility::QueryParams Utility::parseQueryString(absl::string_view url) {
  QueryParams params;
  size_t start = url.find('?');
  if (start == std::string::npos) {
    return params;
  }

  start++;
  while (start < url.size()) {
    size_t end = url.find('&', start);
    if (end == std::string::npos) {
      end = url.size();
    }
    absl::string_view param(url.data() + start, end - start);

    const size_t equal = param.find('=');
    if (equal != std::string::npos) {
      params.emplace(StringUtil::subspan(url, start, start + equal),
                     StringUtil::subspan(url, start + equal + 1, end));
    } else {
      params.emplace(StringUtil::subspan(url, start, end), "");
    }

    start = end + 1;
  }

  return params;
}

const char* Utility::findQueryStringStart(const HeaderString& path) {
  return std::find(path.c_str(), path.c_str() + path.size(), '?');
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
        if (header.key() == Http::Headers::get().Cookie.get().c_str()) {
          // Split the cookie header into individual cookies.
          for (const auto s : StringUtil::splitToken(header.value().c_str(), ";")) {
            // Find the key part of the cookie (i.e. the name of the cookie).
            size_t first_non_space = s.find_first_not_of(" ");
            size_t equals_index = s.find('=');
            if (equals_index == std::string::npos) {
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

bool Utility::hasSetCookie(const HeaderMap& headers, const std::string& key) {

  struct State {
    std::string key_;
    bool ret_;
  };

  State state;
  state.key_ = key;
  state.ret_ = false;

  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // Find the set-cookie headers in the request
        if (header.key() == Http::Headers::get().SetCookie.get().c_str()) {
          const std::string value{header.value().c_str()};
          const size_t equals_index = value.find('=');

          if (equals_index == std::string::npos) {
            // The cookie is malformed if it does not have an `=`.
            return HeaderMap::Iterate::Continue;
          }
          std::string k = value.substr(0, equals_index);
          State* state = static_cast<State*>(context);
          if (k == state->key_) {
            state->ret_ = true;
            return HeaderMap::Iterate::Break;
          }
        }
        return HeaderMap::Iterate::Continue;
      },
      &state);

  return state.ret_;
}

uint64_t Utility::getResponseStatus(const HeaderMap& headers) {
  const HeaderEntry* header = headers.Status();
  uint64_t response_code;
  if (!header || !StringUtil::atoul(headers.Status()->value().c_str(), response_code)) {
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

bool Utility::isWebSocketUpgradeRequest(const HeaderMap& headers) {
  return (isUpgrade(headers) && (0 == StringUtil::caseInsensitiveCompare(
                                          headers.Upgrade()->value().c_str(),
                                          Http::Headers::get().UpgradeValues.WebSocket.c_str())));
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
  return ret;
}

Http1Settings
Utility::parseHttp1Settings(const envoy::api::v2::core::Http1ProtocolOptions& config) {
  Http1Settings ret;
  ret.allow_absolute_url_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, allow_absolute_url, false);
  ret.accept_http_10_ = config.accept_http_10();
  ret.default_host_for_http_10_ = config.default_host_for_http_10();
  return ret;
}

void Utility::sendLocalReply(bool is_grpc, StreamDecoderFilterCallbacks& callbacks,
                             const bool& is_reset, Code response_code,
                             const std::string& body_text) {
  sendLocalReply(is_grpc,
                 [&](HeaderMapPtr&& headers, bool end_stream) -> void {
                   callbacks.encodeHeaders(std::move(headers), end_stream);
                 },
                 [&](Buffer::Instance& data, bool end_stream) -> void {
                   callbacks.encodeData(data, end_stream);
                 },
                 is_reset, response_code, body_text);
}

void Utility::sendLocalReply(
    bool is_grpc, std::function<void(HeaderMapPtr&& headers, bool end_stream)> encode_headers,
    std::function<void(Buffer::Instance& data, bool end_stream)> encode_data, const bool& is_reset,
    Code response_code, const std::string& body_text) {
  // encode_headers() may reset the stream, so the stream must not be reset before calling it.
  ASSERT(!is_reset);
  // Respond with a gRPC trailers-only response if the request is gRPC
  if (is_grpc) {
    HeaderMapPtr response_headers{new HeaderMapImpl{
        {Headers::get().Status, std::to_string(enumToInt(Code::OK))},
        {Headers::get().ContentType, Headers::get().ContentTypeValues.Grpc},
        {Headers::get().GrpcStatus,
         std::to_string(enumToInt(Grpc::Utility::httpToGrpcStatus(enumToInt(response_code))))}}};
    if (!body_text.empty()) {
      // TODO: GrpcMessage should be percent-encoded
      response_headers->insertGrpcMessage().value(body_text);
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

  absl::string_view xff_string(xff_header->value().c_str(), xff_header->value().size());
  static const std::string seperator(",");
  // Ignore the last num_to_skip addresses at the end of XFF.
  for (uint32_t i = 0; i < num_to_skip; i++) {
    std::string::size_type last_comma = xff_string.rfind(seperator);
    if (last_comma == std::string::npos) {
      return {nullptr, false};
    }
    xff_string = xff_string.substr(0, last_comma);
  }
  // The text after the last remaining comma, or the entirety of the string if there
  // is no comma, is the requested IP address.
  std::string::size_type last_comma = xff_string.rfind(seperator);
  if (last_comma != std::string::npos && last_comma + seperator.size() < xff_string.size()) {
    xff_string = xff_string.substr(last_comma + seperator.size());
  }

  // Ignore the whitespace, since they are allowed in HTTP lists (see RFC7239#section-7.1).
  xff_string = StringUtil::ltrim(xff_string);
  xff_string = StringUtil::rtrim(xff_string);

  try {
    // This technically requires a copy because inet_pton takes a null terminated string. In
    // practice, we are working with a view at the end of the owning string, and could pass the
    // raw pointer.
    // TODO(mattklein123 PERF: Avoid the copy here.
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
  }

  NOT_REACHED;
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
  for (auto p : params) {
    absl::StrAppend(&out, delim, p.first, "=", p.second);
    delim = "&";
  }
  return out;
}

} // namespace Http
} // namespace Envoy
