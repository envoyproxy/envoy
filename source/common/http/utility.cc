#include "source/common/http/utility.h"

#include <http_parser.h>

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/status.h"
#include "source/common/http/character_set_validation.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/http2/adapter/http2_protocol.h"

namespace Envoy {
namespace {

// Get request host from the request header map, removing the port if the port
// does not match the scheme, or if port is provided.
absl::string_view processRequestHost(const Http::RequestHeaderMap& headers,
                                     absl::string_view new_scheme, absl::string_view new_port) {

  absl::string_view request_host = headers.getHostValue();
  size_t host_end;
  if (request_host.empty()) {
    return request_host;
  }
  // Detect if IPv6 URI
  if (request_host[0] == '[') {
    host_end = request_host.rfind("]:");
    if (host_end != absl::string_view::npos) {
      host_end += 1; // advance to :
    }
  } else {
    host_end = request_host.rfind(':');
  }

  if (host_end != absl::string_view::npos) {
    absl::string_view request_port = request_host.substr(host_end);
    // In the rare case that X-Forwarded-Proto and scheme disagree (say http URL over an HTTPS
    // connection), do port stripping based on X-Forwarded-Proto so http://foo.com:80 won't
    // have the port stripped when served over TLS.
    absl::string_view request_protocol = headers.getForwardedProtoValue();
    bool remove_port = !new_port.empty();

    if (new_scheme != request_protocol) {
      remove_port |= Http::Utility::schemeIsHttps(request_protocol) && request_port == ":443";
      remove_port |= Http::Utility::schemeIsHttp(request_protocol) && request_port == ":80";
    }

    if (remove_port) {
      return request_host.substr(0, host_end);
    }
  }

  return request_host;
}

} // namespace
namespace Http2 {
namespace Utility {

namespace {

struct SettingsEntry {
  uint16_t settings_id;
  uint32_t value;
};

struct SettingsEntryHash {
  size_t operator()(const SettingsEntry& entry) const {
    return absl::Hash<decltype(entry.settings_id)>()(entry.settings_id);
  }
};

struct SettingsEntryEquals {
  bool operator()(const SettingsEntry& lhs, const SettingsEntry& rhs) const {
    return lhs.settings_id == rhs.settings_id;
  }
};

void validateCustomSettingsParameters(
    const envoy::config::core::v3::Http2ProtocolOptions& options) {
  std::vector<std::string> parameter_collisions, custom_parameter_collisions;
  absl::node_hash_set<SettingsEntry, SettingsEntryHash, SettingsEntryEquals> custom_parameters;
  // User defined and named parameters with the same SETTINGS identifier can not both be set.
  for (const auto& it : options.custom_settings_parameters()) {
    ASSERT(it.identifier().value() <= std::numeric_limits<uint16_t>::max());
    // Check for custom parameter inconsistencies.
    const auto result = custom_parameters.insert(
        {static_cast<uint16_t>(it.identifier().value()), it.value().value()});
    if (!result.second) {
      if (result.first->value != it.value().value()) {
        custom_parameter_collisions.push_back(
            absl::StrCat("0x", absl::Hex(it.identifier().value(), absl::kZeroPad2)));
        // Fall through to allow unbatched exceptions to throw first.
      }
    }
    switch (it.identifier().value()) {
    case http2::adapter::ENABLE_PUSH:
      if (it.value().value() == 1) {
        throwEnvoyExceptionOrPanic(
            "server push is not supported by Envoy and can not be enabled via a "
            "SETTINGS parameter.");
      }
      break;
    case http2::adapter::ENABLE_CONNECT_PROTOCOL:
      // An exception is made for `allow_connect` which can't be checked for presence due to the
      // use of a primitive type (bool).
      throwEnvoyExceptionOrPanic("the \"allow_connect\" SETTINGS parameter must only be configured "
                                 "through the named field");
    case http2::adapter::HEADER_TABLE_SIZE:
      if (options.has_hpack_table_size()) {
        parameter_collisions.push_back("hpack_table_size");
      }
      break;
    case http2::adapter::MAX_CONCURRENT_STREAMS:
      if (options.has_max_concurrent_streams()) {
        parameter_collisions.push_back("max_concurrent_streams");
      }
      break;
    case http2::adapter::INITIAL_WINDOW_SIZE:
      if (options.has_initial_stream_window_size()) {
        parameter_collisions.push_back("initial_stream_window_size");
      }
      break;
    default:
      // Ignore unknown parameters.
      break;
    }
  }

  if (!custom_parameter_collisions.empty()) {
    throwEnvoyExceptionOrPanic(fmt::format(
        "inconsistent HTTP/2 custom SETTINGS parameter(s) detected; identifiers = {{{}}}",
        absl::StrJoin(custom_parameter_collisions, ",")));
  }
  if (!parameter_collisions.empty()) {
    throwEnvoyExceptionOrPanic(fmt::format(
        "the {{{}}} HTTP/2 SETTINGS parameter(s) can not be configured through both named and "
        "custom parameters",
        absl::StrJoin(parameter_collisions, ",")));
  }
}

} // namespace

const uint32_t OptionsLimits::MIN_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::DEFAULT_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::MAX_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::MIN_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::MAX_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES;
const uint32_t OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES;
const uint32_t OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
const uint32_t OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
const uint32_t OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT;

envoy::config::core::v3::Http2ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error) {
  auto ret = initializeAndValidateOptions(options);
  if (!options.has_override_stream_error_on_invalid_http_message() && hcm_stream_error_set) {
    ret.mutable_override_stream_error_on_invalid_http_message()->set_value(
        hcm_stream_error.value());
  }
  return ret;
}

envoy::config::core::v3::Http2ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http2ProtocolOptions& options) {
  envoy::config::core::v3::Http2ProtocolOptions options_clone(options);
  // This will throw an exception when a custom parameter and a named parameter collide.
  validateCustomSettingsParameters(options);

  if (!options.has_override_stream_error_on_invalid_http_message()) {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(
        options.stream_error_on_invalid_http_messaging());
  }

  if (!options_clone.has_hpack_table_size()) {
    options_clone.mutable_hpack_table_size()->set_value(OptionsLimits::DEFAULT_HPACK_TABLE_SIZE);
  }
  ASSERT(options_clone.hpack_table_size().value() <= OptionsLimits::MAX_HPACK_TABLE_SIZE);
  if (!options_clone.has_max_concurrent_streams()) {
    options_clone.mutable_max_concurrent_streams()->set_value(
        OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS);
  }
  ASSERT(
      options_clone.max_concurrent_streams().value() >= OptionsLimits::MIN_MAX_CONCURRENT_STREAMS &&
      options_clone.max_concurrent_streams().value() <= OptionsLimits::MAX_MAX_CONCURRENT_STREAMS);
  if (!options_clone.has_initial_stream_window_size()) {
    options_clone.mutable_initial_stream_window_size()->set_value(
        OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  }
  ASSERT(options_clone.initial_stream_window_size().value() >=
             OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE &&
         options_clone.initial_stream_window_size().value() <=
             OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE);
  if (!options_clone.has_initial_connection_window_size()) {
    options_clone.mutable_initial_connection_window_size()->set_value(
        OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
  }
  ASSERT(options_clone.initial_connection_window_size().value() >=
             OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE &&
         options_clone.initial_connection_window_size().value() <=
             OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE);
  if (!options_clone.has_max_outbound_frames()) {
    options_clone.mutable_max_outbound_frames()->set_value(
        OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES);
  }
  if (!options_clone.has_max_outbound_control_frames()) {
    options_clone.mutable_max_outbound_control_frames()->set_value(
        OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES);
  }
  if (!options_clone.has_max_consecutive_inbound_frames_with_empty_payload()) {
    options_clone.mutable_max_consecutive_inbound_frames_with_empty_payload()->set_value(
        OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD);
  }
  if (!options_clone.has_max_inbound_priority_frames_per_stream()) {
    options_clone.mutable_max_inbound_priority_frames_per_stream()->set_value(
        OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM);
  }
  if (!options_clone.has_max_inbound_window_update_frames_per_data_frame_sent()) {
    options_clone.mutable_max_inbound_window_update_frames_per_data_frame_sent()->set_value(
        OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT);
  }

  return options_clone;
}

} // namespace Utility
} // namespace Http2

namespace Http3 {
namespace Utility {

const uint32_t OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;

envoy::config::core::v3::Http3ProtocolOptions
initializeAndValidateOptions(const envoy::config::core::v3::Http3ProtocolOptions& options,
                             bool hcm_stream_error_set,
                             const ProtobufWkt::BoolValue& hcm_stream_error) {
  if (options.has_override_stream_error_on_invalid_http_message()) {
    return options;
  }
  envoy::config::core::v3::Http3ProtocolOptions options_clone(options);
  if (hcm_stream_error_set) {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(
        hcm_stream_error.value());
  } else {
    options_clone.mutable_override_stream_error_on_invalid_http_message()->set_value(false);
  }
  return options_clone;
}

} // namespace Utility
} // namespace Http3

namespace Http {

static const char kDefaultPath[] = "/";

// If http_parser encounters an IP address [address] as the host it will set the offset and
// length to point to 'address' rather than '[address]'. Fix this by adjusting the offset
// and length to include the brackets.
// @param absolute_url the absolute URL. This is usually of the form // http://host/path
//        but may be host:port for CONNECT requests
// @param offset the offset for the first character of the host. For IPv6 hosts
//        this will point to the first character inside the brackets and will be
//        adjusted to point at the brackets
// @param len the length of the host-and-port field. For IPv6 hosts this will
//        not include the brackets and will be adjusted to do so.
bool maybeAdjustForIpv6(absl::string_view absolute_url, uint64_t& offset, uint64_t& len) {
  // According to https://tools.ietf.org/html/rfc3986#section-3.2.2 the only way a hostname
  // may begin with '[' is if it's an ipv6 address.
  if (offset == 0 || *(absolute_url.data() + offset - 1) != '[') {
    return false;
  }
  // Start one character sooner and end one character later.
  offset--;
  len += 2;
  // HTTP parser ensures that any [ has a closing ]
  ASSERT(absolute_url.length() >= offset + len);
  return true;
}

void forEachCookie(
    const HeaderMap& headers, const LowerCaseString& cookie_header,
    const std::function<bool(absl::string_view, absl::string_view)>& cookie_consumer) {
  const Http::HeaderMap::GetResult cookie_headers = headers.get(cookie_header);

  for (size_t index = 0; index < cookie_headers.size(); index++) {
    auto cookie_header_value = cookie_headers[index]->value().getStringView();

    // Split the cookie header into individual cookies.
    for (const auto& s : StringUtil::splitToken(cookie_header_value, ";")) {
      // Find the key part of the cookie (i.e. the name of the cookie).
      size_t first_non_space = s.find_first_not_of(' ');
      size_t equals_index = s.find('=');
      if (equals_index == absl::string_view::npos) {
        // The cookie is malformed if it does not have an `=`. Continue
        // checking other cookies in this header.
        continue;
      }
      absl::string_view k = s.substr(first_non_space, equals_index - first_non_space);
      absl::string_view v = s.substr(equals_index + 1, s.size() - 1);

      // Cookie values may be wrapped in double quotes.
      // https://tools.ietf.org/html/rfc6265#section-4.1.1
      if (v.size() >= 2 && v.back() == '"' && v[0] == '"') {
        v = v.substr(1, v.size() - 2);
      }

      if (!cookie_consumer(k, v)) {
        return;
      }
    }
  }
}

std::string parseCookie(const HeaderMap& headers, const std::string& key,
                        const LowerCaseString& cookie) {
  std::string value;

  // Iterate over each cookie & return if its value is not empty.
  forEachCookie(headers, cookie, [&key, &value](absl::string_view k, absl::string_view v) -> bool {
    if (key == k) {
      value = std::string{v};
      return false;
    }

    // continue iterating until a cookie that matches `key` is found.
    return true;
  });

  return value;
}

absl::flat_hash_map<std::string, std::string>
Utility::parseCookies(const RequestHeaderMap& headers) {
  return Utility::parseCookies(headers, [](absl::string_view) -> bool { return true; });
}

absl::flat_hash_map<std::string, std::string>
Utility::parseCookies(const RequestHeaderMap& headers,
                      const std::function<bool(absl::string_view)>& key_filter) {
  absl::flat_hash_map<std::string, std::string> cookies;

  forEachCookie(headers, Http::Headers::get().Cookie,
                [&cookies, &key_filter](absl::string_view k, absl::string_view v) -> bool {
                  if (key_filter(k)) {
                    cookies.emplace(k, v);
                  }

                  // continue iterating until all cookies are processed.
                  return true;
                });

  return cookies;
}

bool Utility::Url::containsFragment() { return (component_bitmap_ & (1 << UcFragment)); }

bool Utility::Url::containsUserinfo() { return (component_bitmap_ & (1 << UcUserinfo)); }

bool Utility::Url::initialize(absl::string_view absolute_url, bool is_connect) {
  struct http_parser_url u;
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

  component_bitmap_ = u.field_set;
  scheme_ = absl::string_view(absolute_url.data() + u.field_data[UF_SCHEMA].off,
                              u.field_data[UF_SCHEMA].len);

  uint64_t authority_len = u.field_data[UF_HOST].len;
  if ((u.field_set & (1 << UF_PORT)) == (1 << UF_PORT)) {
    authority_len = authority_len + u.field_data[UF_PORT].len + 1;
  }

  uint64_t authority_beginning = u.field_data[UF_HOST].off;
  const bool is_ipv6 = maybeAdjustForIpv6(absolute_url, authority_beginning, authority_len);
  host_and_port_ = absl::string_view(absolute_url.data() + authority_beginning, authority_len);
  if (is_ipv6 && !parseAuthority(host_and_port_).is_ip_address_) {
    return false;
  }

  // RFC allows the absolute-uri to not end in /, but the absolute path form
  // must start with. Determine if there's a non-zero path, and if so determine
  // the length of the path, query params etc.
  uint64_t path_etc_len = absolute_url.length() - (authority_beginning + hostAndPort().length());
  if (path_etc_len > 0) {
    uint64_t path_beginning = authority_beginning + hostAndPort().length();
    path_and_query_params_ = absl::string_view(absolute_url.data() + path_beginning, path_etc_len);
  } else if (!is_connect) {
    ASSERT((u.field_set & (1 << UF_PATH)) == 0);
    path_and_query_params_ = absl::string_view(kDefaultPath, 1);
  }
  return true;
}

std::string Utility::Url::toString() const {
  return absl::StrCat(scheme_, "://", host_and_port_, path_and_query_params_);
}

void Utility::appendXff(RequestHeaderMap& headers,
                        const Network::Address::Instance& remote_address) {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return;
  }

  headers.appendForwardedFor(remote_address.ip()->addressAsString(), ",");
}

void Utility::appendVia(RequestOrResponseHeaderMap& headers, const std::string& via) {
  // TODO(asraa): Investigate whether it is necessary to append with whitespace here by:
  //     (a) Validating we do not expect whitespace in via headers
  //     (b) Add runtime guarding in case users have upstreams which expect it.
  headers.appendVia(via, ", ");
}

void Utility::updateAuthority(RequestHeaderMap& headers, absl::string_view hostname,
                              const bool append_xfh) {
  const auto host = headers.getHostValue();

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.append_xfh_idempotent")) {
    // Only append to x-forwarded-host if the value was not the last value appended.
    const auto xfh = headers.getForwardedHostValue();

    if (append_xfh && !host.empty()) {
      if (!xfh.empty()) {
        const auto xfh_split = StringUtil::splitToken(xfh, ",");
        if (!xfh_split.empty() && xfh_split.back() != host) {
          headers.appendForwardedHost(host, ",");
        }
      } else {
        headers.appendForwardedHost(host, ",");
      }
    }
  } else {
    if (append_xfh && !host.empty()) {
      headers.appendForwardedHost(host, ",");
    }
  }

  headers.setHost(hostname);
}

std::string Utility::createSslRedirectPath(const RequestHeaderMap& headers) {
  ASSERT(headers.Host());
  ASSERT(headers.Path());
  return fmt::format("https://{}{}", headers.getHostValue(), headers.getPathValue());
}

Utility::QueryParamsMulti Utility::QueryParamsMulti::parseQueryString(absl::string_view url) {
  size_t start = url.find('?');
  if (start == std::string::npos) {
    return {};
  }

  start++;
  return Utility::QueryParamsMulti::parseParameters(url, start, /*decode_params=*/false);
}

Utility::QueryParamsMulti
Utility::QueryParamsMulti::parseAndDecodeQueryString(absl::string_view url) {
  size_t start = url.find('?');
  if (start == std::string::npos) {
    return {};
  }

  start++;
  return Utility::QueryParamsMulti::parseParameters(url, start, /*decode_params=*/true);
}

Utility::QueryParamsMulti Utility::QueryParamsMulti::parseParameters(absl::string_view data,
                                                                     size_t start,
                                                                     bool decode_params) {
  QueryParamsMulti params;

  while (start < data.size()) {
    size_t end = data.find('&', start);
    if (end == std::string::npos) {
      end = data.size();
    }
    absl::string_view param(data.data() + start, end - start);

    const size_t equal = param.find('=');
    if (equal != std::string::npos) {
      const auto param_name = StringUtil::subspan(data, start, start + equal);
      const auto param_value = StringUtil::subspan(data, start + equal + 1, end);
      params.add(decode_params ? PercentEncoding::decode(param_name) : param_name,
                 decode_params ? PercentEncoding::decode(param_value) : param_value);
    } else {
      const auto param_name = StringUtil::subspan(data, start, end);
      params.add(decode_params ? PercentEncoding::decode(param_name) : param_name, "");
    }

    start = end + 1;
  }

  return params;
}

void Utility::QueryParamsMulti::remove(absl::string_view key) { this->data_.erase(key); }

void Utility::QueryParamsMulti::add(absl::string_view key, absl::string_view value) {
  auto result = this->data_.emplace(std::string(key), std::vector<std::string>{std::string(value)});
  if (!result.second) {
    result.first->second.push_back(std::string(value));
  }
}

void Utility::QueryParamsMulti::overwrite(absl::string_view key, absl::string_view value) {
  this->data_[key] = std::vector<std::string>{std::string(value)};
}

absl::optional<std::string> Utility::QueryParamsMulti::getFirstValue(absl::string_view key) const {
  auto it = this->data_.find(key);
  if (it == this->data_.end()) {
    return std::nullopt;
  }

  return absl::optional<std::string>{it->second.at(0)};
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

std::string Utility::stripQueryString(const HeaderString& path) {
  absl::string_view path_str = path.getStringView();
  size_t query_offset = path_str.find('?');
  return {path_str.data(), query_offset != path_str.npos ? query_offset : path_str.size()};
}

std::string Utility::QueryParamsMulti::replaceQueryString(const HeaderString& path) const {
  std::string new_path{Http::Utility::stripQueryString(path)};

  if (!this->data_.empty()) {
    absl::StrAppend(&new_path, this->toString());
  }

  return new_path;
}

std::string Utility::parseCookieValue(const HeaderMap& headers, const std::string& key) {
  // TODO(wbpcode): Modify the headers parameter type to 'RequestHeaderMap'.
  return parseCookie(headers, key, Http::Headers::get().Cookie);
}

std::string Utility::parseSetCookieValue(const Http::HeaderMap& headers, const std::string& key) {
  return parseCookie(headers, key, Http::Headers::get().SetCookie);
}

std::string Utility::makeSetCookieValue(const std::string& key, const std::string& value,
                                        const std::string& path, const std::chrono::seconds max_age,
                                        bool httponly,
                                        const Http::CookieAttributeRefVector attributes) {
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

  for (auto const& attribute : attributes) {
    if (attribute.get().value().empty()) {
      absl::StrAppend(&cookie_value, "; ", attribute.get().name());
    } else {
      absl::StrAppend(&cookie_value, "; ", attribute.get().name(), "=", attribute.get().value());
    }
  }

  if (httponly) {
    absl::StrAppend(&cookie_value, "; HttpOnly");
  }
  return cookie_value;
}

uint64_t Utility::getResponseStatus(const ResponseHeaderMap& headers) {
  auto status = Utility::getResponseStatusOrNullopt(headers);
  if (!status.has_value()) {
    IS_ENVOY_BUG("No status in headers");
    return 0;
  }
  return status.value();
}

absl::optional<uint64_t> Utility::getResponseStatusOrNullopt(const ResponseHeaderMap& headers) {
  const HeaderEntry* header = headers.Status();
  uint64_t response_code;
  if (!header || !absl::SimpleAtoi(headers.getStatusValue(), &response_code)) {
    return absl::nullopt;
  }
  return response_code;
}

bool Utility::isUpgrade(const RequestOrResponseHeaderMap& headers) {
  // In firefox the "Connection" request header value is "keep-alive, Upgrade",
  // we should check if it contains the "Upgrade" token.
  return (headers.Upgrade() &&
          Envoy::StringUtil::caseFindToken(headers.getConnectionValue(), ",",
                                           Http::Headers::get().ConnectionValues.Upgrade));
}

bool Utility::isH2UpgradeRequest(const RequestHeaderMap& headers) {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Connect &&
         headers.Protocol() && !headers.Protocol()->value().empty() &&
         headers.Protocol()->value() != Headers::get().ProtocolValues.Bytestream;
}

bool Utility::isH3UpgradeRequest(const RequestHeaderMap& headers) {
  return isH2UpgradeRequest(headers);
}

bool Utility::isWebSocketUpgradeRequest(const RequestHeaderMap& headers) {
  return (isUpgrade(headers) &&
          absl::EqualsIgnoreCase(headers.getUpgradeValue(),
                                 Http::Headers::get().UpgradeValues.WebSocket));
}

Utility::PreparedLocalReplyPtr Utility::prepareLocalReply(const EncodeFunctions& encode_functions,
                                                          const LocalReplyData& local_reply_data) {
  Code response_code = local_reply_data.response_code_;
  std::string body_text(local_reply_data.body_text_);
  absl::string_view content_type(Headers::get().ContentTypeValues.Text);

  ResponseHeaderMapPtr response_headers{createHeaderMap<ResponseHeaderMapImpl>(
      {{Headers::get().Status, std::to_string(enumToInt(response_code))}})};

  if (encode_functions.modify_headers_) {
    encode_functions.modify_headers_(*response_headers);
  }
  bool has_custom_content_type = false;
  if (encode_functions.rewrite_) {
    std::string content_type_value = std::string(response_headers->getContentTypeValue());
    encode_functions.rewrite_(*response_headers, response_code, body_text, content_type);
    has_custom_content_type = (content_type_value != response_headers->getContentTypeValue());
  }

  if (local_reply_data.is_grpc_) {
    response_headers->setStatus(std::to_string(enumToInt(Code::OK)));
    response_headers->setReferenceContentType(Headers::get().ContentTypeValues.Grpc);

    if (response_headers->getGrpcStatusValue().empty()) {
      response_headers->setGrpcStatus(std::to_string(
          enumToInt(local_reply_data.grpc_status_
                        ? local_reply_data.grpc_status_.value()
                        : Grpc::Utility::httpToGrpcStatus(enumToInt(response_code)))));
    }

    if (!body_text.empty() && !local_reply_data.is_head_request_) {
      // TODO(dio): Probably it is worth to consider caching the encoded message based on gRPC
      // status.
      // JsonFormatter adds a '\n' at the end. For header value, it should be removed.
      // https://github.com/envoyproxy/envoy/blob/main/source/common/formatter/substitution_formatter.cc#L129
      if (content_type == Headers::get().ContentTypeValues.Json &&
          body_text[body_text.length() - 1] == '\n') {
        body_text = body_text.substr(0, body_text.length() - 1);
      }
      response_headers->setGrpcMessage(PercentEncoding::encode(body_text));
    }
    // The `modify_headers` function may have added content-length, remove it.
    response_headers->removeContentLength();

    // TODO(kbaichoo): In C++20 we will be able to use make_unique with
    // aggregate initialization.
    // NOLINTNEXTLINE(modernize-make-unique)
    return PreparedLocalReplyPtr(new PreparedLocalReply{
        local_reply_data.is_grpc_, local_reply_data.is_head_request_, std::move(response_headers),
        std::move(body_text), encode_functions.encode_headers_, encode_functions.encode_data_});
  }

  if (!body_text.empty()) {
    response_headers->setContentLength(body_text.size());
    // If the content-type is not set, set it.
    // Alternately if the `rewrite` function has changed body_text and the config didn't explicitly
    // set a content type header, set the content type to be based on the changed body.
    if (response_headers->ContentType() == nullptr ||
        (body_text != local_reply_data.body_text_ && !has_custom_content_type)) {
      response_headers->setReferenceContentType(content_type);
    }
  } else {
    response_headers->removeContentLength();
    response_headers->removeContentType();
  }

  // TODO(kbaichoo): In C++20 we will be able to use make_unique with
  // aggregate initialization.
  // NOLINTNEXTLINE(modernize-make-unique)
  return PreparedLocalReplyPtr(new PreparedLocalReply{
      local_reply_data.is_grpc_, local_reply_data.is_head_request_, std::move(response_headers),
      std::move(body_text), encode_functions.encode_headers_, encode_functions.encode_data_});
}

void Utility::encodeLocalReply(const bool& is_reset, PreparedLocalReplyPtr prepared_local_reply) {
  ASSERT(prepared_local_reply != nullptr);
  ResponseHeaderMapPtr response_headers{std::move(prepared_local_reply->response_headers_)};

  if (prepared_local_reply->is_grpc_request_) {
    // Trailers only response
    prepared_local_reply->encode_headers_(std::move(response_headers), true);
    return;
  }

  if (prepared_local_reply->is_head_request_) {
    prepared_local_reply->encode_headers_(std::move(response_headers), true);
    return;
  }

  const bool bodyless_response = prepared_local_reply->response_body_.empty();
  prepared_local_reply->encode_headers_(std::move(response_headers), bodyless_response);
  // encode_headers() may have changed the referenced is_reset so we need to test it
  if (!bodyless_response && !is_reset) {
    Buffer::OwnedImpl buffer(prepared_local_reply->response_body_);
    prepared_local_reply->encode_data_(buffer, true);
  }
}

void Utility::sendLocalReply(const bool& is_reset, const EncodeFunctions& encode_functions,
                             const LocalReplyData& local_reply_data) {
  // encode_headers() may reset the stream, so the stream must not be reset before calling it.
  ASSERT(!is_reset);
  PreparedLocalReplyPtr prepared_local_reply =
      prepareLocalReply(encode_functions, local_reply_data);

  encodeLocalReply(is_reset, std::move(prepared_local_reply));
}

Utility::GetLastAddressFromXffInfo
Utility::getLastAddressFromXFF(const Http::RequestHeaderMap& request_headers,
                               uint32_t num_to_skip) {
  const auto xff_header = request_headers.ForwardedFor();
  if (xff_header == nullptr) {
    return {nullptr, false};
  }

  absl::string_view xff_string(xff_header->value().getStringView());
  static constexpr absl::string_view separator(",");
  // Ignore the last num_to_skip addresses at the end of XFF.
  for (uint32_t i = 0; i < num_to_skip; i++) {
    const absl::string_view::size_type last_comma = xff_string.rfind(separator);
    if (last_comma == absl::string_view::npos) {
      return {nullptr, false};
    }
    xff_string = xff_string.substr(0, last_comma);
  }
  // The text after the last remaining comma, or the entirety of the string if there
  // is no comma, is the requested IP address.
  const absl::string_view::size_type last_comma = xff_string.rfind(separator);
  if (last_comma != absl::string_view::npos && last_comma + separator.size() < xff_string.size()) {
    xff_string = xff_string.substr(last_comma + separator.size());
  }

  // Ignore the whitespace, since they are allowed in HTTP lists (see RFC7239#section-7.1).
  xff_string = StringUtil::ltrim(xff_string);
  xff_string = StringUtil::rtrim(xff_string);

  // This technically requires a copy because inet_pton takes a null terminated string. In
  // practice, we are working with a view at the end of the owning string, and could pass the
  // raw pointer.
  // TODO(mattklein123) PERF: Avoid the copy here.
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::parseInternetAddressNoThrow(std::string(xff_string));
  if (address != nullptr) {
    return {address, last_comma == absl::string_view::npos && num_to_skip == 0};
  }
  return {nullptr, false};
}

bool Utility::sanitizeConnectionHeader(Http::RequestHeaderMap& headers) {
  static constexpr size_t MAX_ALLOWED_NOMINATED_HEADERS = 10;
  static constexpr size_t MAX_ALLOWED_TE_VALUE_SIZE = 256;

  // Remove any headers nominated by the Connection header. The TE header
  // is sanitized and removed only if it's empty after removing unsupported values
  // See https://github.com/envoyproxy/envoy/issues/8623
  const auto& cv = Http::Headers::get().ConnectionValues;
  const auto& connection_header_value = headers.Connection()->value();

  StringUtil::CaseUnorderedSet headers_to_remove{};
  std::vector<absl::string_view> connection_header_tokens =
      StringUtil::splitToken(connection_header_value.getStringView(), ",", false);

  // If we have 10 or more nominated headers, fail this request
  if (connection_header_tokens.size() >= MAX_ALLOWED_NOMINATED_HEADERS) {
    ENVOY_LOG_MISC(trace, "Too many nominated headers in request");
    return false;
  }

  // Split the connection header and evaluate each nominated header
  for (const auto& token : connection_header_tokens) {

    const auto token_sv = StringUtil::trim(token);

    // Build the LowerCaseString for header lookup
    const LowerCaseString lcs_header_to_remove{std::string(token_sv)};

    // If the Connection token value is not a nominated header, ignore it here since
    // the connection header is removed elsewhere when the H1 request is upgraded to H2
    if ((lcs_header_to_remove.get() == cv.Close) ||
        (lcs_header_to_remove.get() == cv.Http2Settings) ||
        (lcs_header_to_remove.get() == cv.KeepAlive) ||
        (lcs_header_to_remove.get() == cv.Upgrade)) {
      continue;
    }

    // By default we will remove any nominated headers
    bool keep_header = false;

    // Determine whether the nominated header contains invalid values
    HeaderMap::GetResult nominated_header;

    if (lcs_header_to_remove == Http::Headers::get().Connection) {
      // Remove the connection header from the nominated tokens if it's self nominated
      // The connection header itself is *not removed*
      ENVOY_LOG_MISC(trace, "Skipping self nominated header [{}]", token_sv);
      keep_header = true;
      headers_to_remove.emplace(token_sv);

    } else if ((lcs_header_to_remove == Http::Headers::get().ForwardedFor) ||
               (lcs_header_to_remove == Http::Headers::get().ForwardedHost) ||
               (lcs_header_to_remove == Http::Headers::get().ForwardedProto) ||
               !token_sv.find(':')) {

      // An attacker could nominate an X-Forwarded* header, and its removal may mask
      // the origin of the incoming request and potentially alter its handling.
      // Additionally, pseudo headers should never be nominated. In both cases, we
      // should fail the request.
      // See: https://nathandavison.com/blog/abusing-http-hop-by-hop-request-headers

      ENVOY_LOG_MISC(trace, "Invalid nomination of {} header", token_sv);
      return false;
    } else {
      // Examine the value of all other nominated headers
      nominated_header = headers.get(lcs_header_to_remove);
    }

    if (!nominated_header.empty()) {
      // NOTE: The TE header is an inline header, so by definition if we operate on it there can
      // only be a single value. In all other cases we remove the nominated header.
      auto nominated_header_value_sv = nominated_header[0]->value().getStringView();

      const bool is_te_header = (lcs_header_to_remove == Http::Headers::get().TE);

      // reject the request if the TE header is too large
      if (is_te_header && (nominated_header_value_sv.size() >= MAX_ALLOWED_TE_VALUE_SIZE)) {
        ENVOY_LOG_MISC(trace, "TE header contains a value that exceeds the allowable length");
        return false;
      }

      if (is_te_header) {
        ASSERT(nominated_header.size() == 1);
        for (const auto& header_value :
             StringUtil::splitToken(nominated_header_value_sv, ",", false)) {

          const absl::string_view header_sv = StringUtil::trim(header_value);

          // If trailers exist in the TE value tokens, keep the header, removing any other values
          // that may exist
          if (StringUtil::CaseInsensitiveCompare()(header_sv,
                                                   Http::Headers::get().TEValues.Trailers)) {
            keep_header = true;
            break;
          }
        }

        if (keep_header) {
          headers.setTE(Http::Headers::get().TEValues.Trailers);
        }
      }
    }

    if (!keep_header) {
      ENVOY_LOG_MISC(trace, "Removing nominated header [{}]", token_sv);
      headers.remove(lcs_header_to_remove);
      headers_to_remove.emplace(token_sv);
    }
  }

  // Lastly remove extra nominated headers from the Connection header
  if (!headers_to_remove.empty()) {
    const std::string new_value = StringUtil::removeTokens(connection_header_value.getStringView(),
                                                           ",", headers_to_remove, ",");

    if (new_value.empty()) {
      headers.removeConnection();
    } else {
      headers.setConnection(new_value);
    }
  }

  return true;
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

  return EMPTY_STRING;
}

std::string Utility::buildOriginalUri(const Http::RequestHeaderMap& request_headers,
                                      const absl::optional<uint32_t> max_path_length) {
  if (!request_headers.Path()) {
    return "";
  }
  absl::string_view path(request_headers.EnvoyOriginalPath()
                             ? request_headers.getEnvoyOriginalPathValue()
                             : request_headers.getPathValue());

  if (max_path_length.has_value() && path.length() > max_path_length) {
    path = path.substr(0, max_path_length.value());
  }

  return absl::StrCat(request_headers.getSchemeValue(), "://", request_headers.getHostValue(),
                      path);
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
  const auto path_pos = uri.find('/', host_pos);
  if (path_pos == std::string::npos) {
    // If uri doesn't have "/", the whole string is treated as host.
    host = uri.substr(host_pos);
    path = "/";
  } else {
    host = uri.substr(host_pos, path_pos - host_pos);
    path = uri.substr(path_pos);
  }
}

std::string Utility::localPathFromFilePath(const absl::string_view& file_path) {
  if (file_path.size() >= 3 && file_path[1] == ':' && file_path[2] == '/' &&
      std::isalpha(file_path[0])) {
    return std::string(file_path);
  }
  return absl::StrCat("/", file_path);
}

RequestMessagePtr Utility::prepareHeaders(const envoy::config::core::v3::HttpUri& http_uri) {
  absl::string_view host, path;
  extractHostPathFromUri(http_uri.uri(), host, path);

  RequestMessagePtr message(new RequestMessageImpl());
  message->headers().setPath(path);
  message->headers().setHost(host);

  return message;
}

std::string Utility::QueryParamsMulti::toString() const {
  std::string out;
  std::string delim = "?";
  for (const auto& p : this->data_) {
    for (const auto& v : p.second) {
      absl::StrAppend(&out, delim, p.first, "=", v);
      delim = "&";
    }
  }
  return out;
}

const std::string Utility::resetReasonToString(const Http::StreamResetReason reset_reason) {
  switch (reset_reason) {
  case Http::StreamResetReason::LocalConnectionFailure:
    return "local connection failure";
  case Http::StreamResetReason::RemoteConnectionFailure:
    return "remote connection failure";
  case Http::StreamResetReason::ConnectionTimeout:
    return "connection timeout";
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
  case Http::StreamResetReason::ConnectError:
    return "remote error with CONNECT request";
  case Http::StreamResetReason::ProtocolError:
    return "protocol error";
  case Http::StreamResetReason::OverloadManager:
    return "overload manager reset";
  }

  return "";
}

void Utility::transformUpgradeRequestFromH1toH2(RequestHeaderMap& headers) {
  ASSERT(Utility::isUpgrade(headers));

  headers.setReferenceMethod(Http::Headers::get().MethodValues.Connect);
  headers.setProtocol(headers.getUpgradeValue());
  headers.removeUpgrade();
  headers.removeConnection();
  // nghttp2 rejects upgrade requests/responses with content length, so strip
  // any unnecessary content length header.
  if (headers.getContentLengthValue() == "0") {
    headers.removeContentLength();
  }
}

void Utility::transformUpgradeRequestFromH1toH3(RequestHeaderMap& headers) {
  transformUpgradeRequestFromH1toH2(headers);
}

void Utility::transformUpgradeResponseFromH1toH2(ResponseHeaderMap& headers) {
  if (getResponseStatus(headers) == 101) {
    headers.setStatus(200);
  }
  headers.removeUpgrade();
  headers.removeConnection();
  if (headers.getContentLengthValue() == "0") {
    headers.removeContentLength();
  }
}

void Utility::transformUpgradeResponseFromH1toH3(ResponseHeaderMap& headers) {
  transformUpgradeResponseFromH1toH2(headers);
}

void Utility::transformUpgradeRequestFromH2toH1(RequestHeaderMap& headers) {
  ASSERT(Utility::isH2UpgradeRequest(headers));

  headers.setReferenceMethod(Http::Headers::get().MethodValues.Get);
  headers.setUpgrade(headers.getProtocolValue());
  headers.setReferenceConnection(Http::Headers::get().ConnectionValues.Upgrade);
  headers.removeProtocol();
}

void Utility::transformUpgradeRequestFromH3toH1(RequestHeaderMap& headers) {
  transformUpgradeRequestFromH2toH1(headers);
}

void Utility::transformUpgradeResponseFromH2toH1(ResponseHeaderMap& headers,
                                                 absl::string_view upgrade) {
  if (getResponseStatus(headers) == 200) {
    headers.setUpgrade(upgrade);
    headers.setReferenceConnection(Http::Headers::get().ConnectionValues.Upgrade);
    headers.setStatus(101);
  }
}

void Utility::transformUpgradeResponseFromH3toH1(ResponseHeaderMap& headers,
                                                 absl::string_view upgrade) {
  transformUpgradeResponseFromH2toH1(headers, upgrade);
}

std::string Utility::PercentEncoding::encode(absl::string_view value,
                                             absl::string_view reserved_chars) {
  absl::flat_hash_set<char> reserved_char_set{reserved_chars.begin(), reserved_chars.end()};
  for (size_t i = 0; i < value.size(); ++i) {
    const char& ch = value[i];
    // The escaping characters are defined in
    // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses.
    //
    // We do checking for each char in the string. If the current char is included in the defined
    // escaping characters, we jump to "the slow path" (append the char [encoded or not encoded]
    // to the returned string one by one) started from the current index.
    if (ch < ' ' || ch >= '~' || reserved_char_set.find(ch) != reserved_char_set.end()) {
      return PercentEncoding::encode(value, i, reserved_char_set);
    }
  }
  return std::string(value);
}

std::string Utility::PercentEncoding::encode(absl::string_view value, const size_t index,
                                             const absl::flat_hash_set<char>& reserved_char_set) {
  std::string encoded;
  if (index > 0) {
    absl::StrAppend(&encoded, value.substr(0, index));
  }

  for (size_t i = index; i < value.size(); ++i) {
    const char& ch = value[i];
    if (ch < ' ' || ch >= '~' || reserved_char_set.find(ch) != reserved_char_set.end()) {
      // For consistency, URI producers should use uppercase hexadecimal digits for all
      // percent-encodings. https://tools.ietf.org/html/rfc3986#section-2.1.
      absl::StrAppend(&encoded, fmt::format("%{:02X}", static_cast<const unsigned char&>(ch)));
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
      if (absl::ascii_isxdigit(hi) && absl::ascii_isxdigit(lo)) {
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
    }
    decoded.push_back(ch);
  }
  return decoded;
}

namespace {
// %-encode all ASCII character codepoints, EXCEPT:
// ALPHA | DIGIT | * | - | . | _
// SPACE is encoded as %20, NOT as the + character
constexpr std::array<uint32_t, 8> kUrlEncodedCharTable = {
    // control characters
    0b11111111111111111111111111111111,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b11111111110110010000000000111111,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b10000000000000000000000000011110,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b10000000000000000000000000011111,
    // extended ascii
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
    0b11111111111111111111111111111111,
};

constexpr std::array<uint32_t, 8> kUrlDecodedCharTable = {
    // control characters
    0b00000000000000000000000000000000,
    // !"#$%&'()*+,-./0123456789:;<=>?
    0b01011111111111111111111111110101,
    //@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
    0b11111111111111111111111111110101,
    //`abcdefghijklmnopqrstuvwxyz{|}~
    0b11111111111111111111111111100010,
    // extended ascii
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
    0b00000000000000000000000000000000,
};

bool shouldPercentEncodeChar(char c) { return testCharInTable(kUrlEncodedCharTable, c); }

bool shouldPercentDecodeChar(char c) { return testCharInTable(kUrlDecodedCharTable, c); }
} // namespace

std::string Utility::PercentEncoding::urlEncodeQueryParameter(absl::string_view value) {
  std::string encoded;
  encoded.reserve(value.size());
  for (char ch : value) {
    if (shouldPercentEncodeChar(ch)) {
      // For consistency, URI producers should use uppercase hexadecimal digits for all
      // percent-encodings. https://tools.ietf.org/html/rfc3986#section-2.1.
      absl::StrAppend(&encoded, fmt::format("%{:02X}", static_cast<const unsigned char&>(ch)));
    } else {
      encoded.push_back(ch);
    }
  }
  return encoded;
}

std::string Utility::PercentEncoding::urlDecodeQueryParameter(absl::string_view encoded) {
  std::string decoded;
  decoded.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i) {
    char ch = encoded[i];
    if (ch == '%' && i + 2 < encoded.size()) {
      const char& hi = encoded[i + 1];
      const char& lo = encoded[i + 2];
      if (absl::ascii_isxdigit(hi) && absl::ascii_isxdigit(lo)) {
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
        if (shouldPercentDecodeChar(ch)) {
          // Decode the character only if it is present in the characters_to_decode
          decoded.push_back(ch);
        } else {
          // Otherwise keep it as is.
          decoded.push_back('%');
          decoded.push_back(hi);
          decoded.push_back(lo);
        }
        i += 2;
      }
    } else {
      decoded.push_back(ch);
    }
  }
  return decoded;
}

Utility::AuthorityAttributes Utility::parseAuthority(absl::string_view host) {
  // First try to see if there is a port included. This also checks to see that there is not a ']'
  // as the last character which is indicative of an IPv6 address without a port. This is a best
  // effort attempt.
  const auto colon_pos = host.rfind(':');
  absl::string_view host_to_resolve = host;
  absl::optional<uint16_t> port;
  if (colon_pos != absl::string_view::npos && host_to_resolve.back() != ']') {
    const absl::string_view string_view_host = host;
    host_to_resolve = string_view_host.substr(0, colon_pos);
    const auto port_str = string_view_host.substr(colon_pos + 1);
    uint64_t port64;
    if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
      // Just attempt to resolve whatever we were given. This will very likely fail.
      host_to_resolve = host;
    } else {
      port = static_cast<uint16_t>(port64);
    }
  }

  // Now see if this is an IP address. We need to know this because some things (such as setting
  // SNI) are special cased if this is an IP address. Either way, we still go through the normal
  // resolver flow. We could short-circuit the DNS resolver in this case, but the extra code to do
  // so is not worth it since the DNS resolver should handle it for us.
  bool is_ip_address = false;
  absl::string_view potential_ip_address = host_to_resolve;
  // TODO(mattklein123): Optimally we would support bracket parsing in parseInternetAddress(),
  // but we still need to trim the brackets to send the IPv6 address into the DNS resolver. For
  // now, just do all the trimming here, but in the future we should consider whether we can
  // have unified [] handling as low as possible in the stack.
  if (!potential_ip_address.empty() && potential_ip_address.front() == '[' &&
      potential_ip_address.back() == ']') {
    potential_ip_address.remove_prefix(1);
    potential_ip_address.remove_suffix(1);
  }
  if (Network::Utility::parseInternetAddressNoThrow(std::string(potential_ip_address)) != nullptr) {
    is_ip_address = true;
    host_to_resolve = potential_ip_address;
  }

  return {is_ip_address, host_to_resolve, port};
}

void Utility::validateCoreRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy) {
  if (retry_policy.has_retry_back_off()) {
    const auto& core_back_off = retry_policy.retry_back_off();

    uint64_t base_interval_ms = PROTOBUF_GET_MS_REQUIRED(core_back_off, base_interval);
    uint64_t max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(core_back_off, max_interval, base_interval_ms * 10);

    if (max_interval_ms < base_interval_ms) {
      throwEnvoyExceptionOrPanic("max_interval must be greater than or equal to the base_interval");
    }
  }
}

envoy::config::route::v3::RetryPolicy
Utility::convertCoreToRouteRetryPolicy(const envoy::config::core::v3::RetryPolicy& retry_policy,
                                       const std::string& retry_on) {
  envoy::config::route::v3::RetryPolicy route_retry_policy;
  constexpr uint64_t default_base_interval_ms = 1000;
  constexpr uint64_t default_max_interval_ms = 10 * default_base_interval_ms;

  uint64_t base_interval_ms = default_base_interval_ms;
  uint64_t max_interval_ms = default_max_interval_ms;

  if (retry_policy.has_retry_back_off()) {
    const auto& core_back_off = retry_policy.retry_back_off();

    base_interval_ms = PROTOBUF_GET_MS_REQUIRED(core_back_off, base_interval);
    max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(core_back_off, max_interval, base_interval_ms * 10);

    if (max_interval_ms < base_interval_ms) {
      ENVOY_BUG(false, "max_interval must be greater than or equal to the base_interval");
      base_interval_ms = max_interval_ms / 2;
    }
  }

  route_retry_policy.mutable_num_retries()->set_value(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(retry_policy, num_retries, 1));

  auto* route_mutable_back_off = route_retry_policy.mutable_retry_back_off();

  route_mutable_back_off->mutable_base_interval()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(base_interval_ms));
  route_mutable_back_off->mutable_max_interval()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(max_interval_ms));

  // set all the other fields with appropriate values.
  route_retry_policy.set_retry_on(retry_on);
  route_retry_policy.mutable_per_try_timeout()->CopyFrom(
      route_retry_policy.retry_back_off().max_interval());

  return route_retry_policy;
}

bool Utility::isSafeRequest(const Http::RequestHeaderMap& request_headers) {
  absl::string_view method = request_headers.getMethodValue();
  return method == Http::Headers::get().MethodValues.Get ||
         method == Http::Headers::get().MethodValues.Head ||
         method == Http::Headers::get().MethodValues.Options ||
         method == Http::Headers::get().MethodValues.Trace;
}

Http::Code Utility::maybeRequestTimeoutCode(bool remote_decode_complete) {
  return remote_decode_complete ? Http::Code::GatewayTimeout
                                // Http::Code::RequestTimeout is more expensive because HTTP1 client
                                // cannot use the connection any more.
                                : Http::Code::RequestTimeout;
}

bool Utility::schemeIsValid(const absl::string_view scheme) {
  return schemeIsHttp(scheme) || schemeIsHttps(scheme);
}

bool Utility::schemeIsHttp(const absl::string_view scheme) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.handle_uppercase_scheme")) {
    return scheme == Headers::get().SchemeValues.Http;
  }
  return absl::EqualsIgnoreCase(scheme, Headers::get().SchemeValues.Http);
}

bool Utility::schemeIsHttps(const absl::string_view scheme) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.handle_uppercase_scheme")) {
    return scheme == Headers::get().SchemeValues.Https;
  }
  return absl::EqualsIgnoreCase(scheme, Headers::get().SchemeValues.Https);
}

std::string Utility::newUri(::Envoy::OptRef<const Utility::RedirectConfig> redirect_config,
                            const Http::RequestHeaderMap& headers) {
  absl::string_view final_scheme;
  absl::string_view final_host;
  absl::string_view final_port;
  absl::string_view final_path;

  if (redirect_config.has_value() && !redirect_config->scheme_redirect_.empty()) {
    final_scheme = redirect_config->scheme_redirect_;
  } else if (redirect_config.has_value() && redirect_config->https_redirect_) {
    final_scheme = Http::Headers::get().SchemeValues.Https;
  } else {
    // Serve the redirect URL based on the scheme of the original URL, not the
    // security of the underlying connection.
    final_scheme = headers.getSchemeValue();
  }

  if (redirect_config.has_value() && !redirect_config->port_redirect_.empty()) {
    final_port = redirect_config->port_redirect_;
  } else {
    final_port = "";
  }

  if (redirect_config.has_value() && !redirect_config->host_redirect_.empty()) {
    final_host = redirect_config->host_redirect_;
  } else {
    ASSERT(headers.Host());
    final_host = processRequestHost(headers, final_scheme, final_port);
  }

  std::string final_path_value;
  if (redirect_config.has_value() && !redirect_config->path_redirect_.empty()) {
    // The path_redirect query string, if any, takes precedence over the request's query string,
    // and it will not be stripped regardless of `strip_query`.
    if (redirect_config->path_redirect_has_query_) {
      final_path = redirect_config->path_redirect_;
    } else {
      const absl::string_view current_path = headers.getPathValue();
      const size_t path_end = current_path.find('?');
      const bool current_path_has_query = path_end != absl::string_view::npos;
      if (current_path_has_query) {
        final_path_value = redirect_config->path_redirect_;
        final_path_value.append(current_path.data() + path_end, current_path.length() - path_end);
        final_path = final_path_value;
      } else {
        final_path = redirect_config->path_redirect_;
      }
    }
  } else {
    final_path = headers.getPathValue();
  }

  if (!absl::StartsWith(final_path, "/")) {
    final_path_value = absl::StrCat("/", final_path);
    final_path = final_path_value;
  }

  if (redirect_config.has_value() && !redirect_config->path_redirect_has_query_ &&
      redirect_config->strip_query_) {
    const size_t path_end = final_path.find('?');
    if (path_end != absl::string_view::npos) {
      final_path = final_path.substr(0, path_end);
    }
  }

  return fmt::format("{}://{}{}{}", final_scheme, final_host, final_port, final_path);
}

bool Utility::isValidRefererValue(absl::string_view value) {

  // First, we try to parse it as an absolute URL and
  // ensure that there is no fragment or userinfo.
  // If that fails, we can just parse it as a relative reference
  // and expect no fragment
  // NOTE: "about:blank" is incorrectly rejected here, because
  // url.initialize uses http_parser_parse_url, which requires
  // a host to be present if there is a schema.
  Utility::Url url;

  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http_allow_partial_urls_in_referer")) {
    if (url.initialize(value, false)) {
      return true;
    }
    return false;
  }

  if (url.initialize(value, false)) {
    return !(url.containsFragment() || url.containsUserinfo());
  }

  bool seen_slash = false;

  for (char c : value) {
    switch (c) {
    case ':':
      if (!seen_slash) {
        // First path segment cannot contain ':'
        // https://www.rfc-editor.org/rfc/rfc3986#section-3.3
        return false;
      }
      continue;
    case '/':
      seen_slash = true;
      continue;
    default:
      if (!testCharInTable(kUriQueryAndFragmentCharTable, c)) {
        return false;
      }
    }
  }

  return true;
}

} // namespace Http
} // namespace Envoy
