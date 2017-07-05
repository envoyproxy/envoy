#include "common/http/utility.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {

void Utility::appendXff(HeaderMap& headers, const Network::Address::Instance& remote_address) {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return;
  }

  // TODO PERF: Append and do not copy.
  HeaderEntry* header = headers.ForwardedFor();
  std::string forwarded_for = header ? header->value().c_str() : "";
  if (!forwarded_for.empty()) {
    forwarded_for += ", ";
  }

  forwarded_for += remote_address.ip()->addressAsString();
  headers.insertForwardedFor().value(forwarded_for);
}

std::string Utility::createSslRedirectPath(const HeaderMap& headers) {
  ASSERT(headers.Host());
  ASSERT(headers.Path());
  return fmt::format("https://{}{}", headers.Host()->value().c_str(),
                     headers.Path()->value().c_str());
}

Utility::QueryParams Utility::parseQueryString(const std::string& url) {
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

    size_t equal = url.find('=', start);
    if (equal != std::string::npos) {
      params.emplace(StringUtil::subspan(url, start, equal),
                     StringUtil::subspan(url, equal + 1, end));
    } else {
      params.emplace(StringUtil::subspan(url, start, end), "");
    }

    start = end + 1;
  }

  return params;
}

std::string Utility::parseCookieValue(const HeaderMap& headers, const std::string& key) {

  struct State {
    std::string key_;
    std::string ret_;
  };

  State state;
  state.key_ = key;

  headers.iterate(
      [](const HeaderEntry& header, void* context) -> void {
        // Find the cookie headers in the request (typically, there's only one).
        if (header.key() == Http::Headers::get().Cookie.get().c_str()) {
          // Split the cookie header into individual cookies.
          for (const std::string& s : StringUtil::split(std::string{header.value().c_str()}, ';')) {
            // Find the key part of the cookie (i.e. the name of the cookie).
            size_t first_non_space = s.find_first_not_of(" ");
            size_t equals_index = s.find('=');
            if (equals_index == std::string::npos) {
              // The cookie is malformed if it does not have an `=`. Continue
              // checking other cookies in this header.
              continue;
            }
            std::string k = s.substr(first_non_space, equals_index - first_non_space);
            State* state = static_cast<State*>(context);
            // If the key matches, parse the value from the rest of the cookie string.
            if (k == state->key_) {
              std::string v = s.substr(equals_index + 1, s.size() - 1);

              // Cookie values may be wrapped in double quotes.
              // https://tools.ietf.org/html/rfc6265#section-4.1.1
              if (v.size() >= 2 && v.back() == '"' && v[0] == '"') {
                v = v.substr(1, v.size() - 2);
              }
              state->ret_ = v;
              return;
            }
          }
        }
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

bool Utility::isInternalRequest(const HeaderMap& headers) {
  // The current header
  const HeaderEntry* forwarded_for = headers.ForwardedFor();
  if (!forwarded_for) {
    return false;
  }

  return Network::Utility::isInternalAddress(forwarded_for->value().c_str());
}

Http2Settings Utility::parseHttp2Settings(const Json::Object& config) {
  Http2Settings ret;

  Json::ObjectSharedPtr http2_settings = config.getObject("http2_settings", true);
  ret.hpack_table_size_ =
      http2_settings->getInteger("hpack_table_size", Http::Http2Settings::DEFAULT_HPACK_TABLE_SIZE);
  ret.max_concurrent_streams_ = http2_settings->getInteger(
      "max_concurrent_streams", Http::Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS);
  ret.initial_stream_window_size_ = http2_settings->getInteger(
      "initial_stream_window_size", Http::Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE);
  ret.initial_connection_window_size_ =
      http2_settings->getInteger("initial_connection_window_size",
                                 Http::Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);

  // http_codec_options config is DEPRECATED
  std::string options = config.getString("http_codec_options", "");
  if (options != "") {
    spdlog::logger& logger = Logger::Registry::getLog(Logger::Id::config);
    logger.warn("'http_codec_options' is DEPRECATED, please use 'http2_settings' instead");
  }

  for (const std::string& option : StringUtil::split(options, ',')) {
    if (option == "no_compression") {
      if (http2_settings->hasObject("hpack_table_size") && ret.hpack_table_size_ != 0) {
        throw EnvoyException(
            "'http_codec_options.no_compression' conflicts with 'http2_settings.hpack_table_size'");
      }
      ret.hpack_table_size_ = 0;
    } else {
      throw EnvoyException(fmt::format("unknown http codec option '{}'", option));
    }
  }

  return ret;
}

void Utility::sendLocalReply(StreamDecoderFilterCallbacks& callbacks, Code response_code,
                             const std::string& body_text) {
  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(response_code))}}};
  if (!body_text.empty()) {
    response_headers->insertContentLength().value(body_text.size());
    response_headers->insertContentType().value(Headers::get().ContentTypeValues.Text);
  }

  callbacks.encodeHeaders(std::move(response_headers), body_text.empty());
  if (!body_text.empty()) {
    Buffer::OwnedImpl buffer(body_text);
    callbacks.encodeData(buffer, true);
  }
}

void Utility::sendRedirect(StreamDecoderFilterCallbacks& callbacks, const std::string& new_path) {
  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(Code::MovedPermanently))},
                        {Headers::get().Location, new_path}}};

  callbacks.encodeHeaders(std::move(response_headers), true);
}

std::string Utility::getLastAddressFromXFF(const Http::HeaderMap& request_headers) {
  if (!request_headers.ForwardedFor()) {
    return EMPTY_STRING;
  }

  std::vector<std::string> xff_address_list =
      StringUtil::split(request_headers.ForwardedFor()->value().c_str(), ", ");

  if (xff_address_list.empty()) {
    return EMPTY_STRING;
  }
  return xff_address_list.back();
}

} // namespace Http
} // namespace Envoy
