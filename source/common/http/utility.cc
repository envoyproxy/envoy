#include "utility.h"

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

  headers.iterate([](const HeaderEntry& header, void* context) -> void {
    if (header.key() == Http::Headers::get().Cookie.get().c_str()) {
      for (const std::string& s : StringUtil::split(std::string{header.value().c_str()}, ';')) {
        size_t first_non_space = s.find_first_not_of(" ");
        size_t equals_index = s.find('=');
        std::string k = s.substr(first_non_space, equals_index - first_non_space);
        State* state = static_cast<State*>(context);
        if (k == state->key_) {
          state->ret_ = s.substr(equals_index + 1, s.size() - 1);
          return;
        }
      }
    }
  }, &state);

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

uint64_t Utility::parseCodecOptions(const Json::Object& config) {
  uint64_t ret = 0;
  std::string options = config.getString("http_codec_options", "");
  for (const std::string& option : StringUtil::split(options, ',')) {
    if (option == "no_compression") {
      ret |= CodecOptions::NoCompression;
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
      StringUtil::split(request_headers.ForwardedFor()->value().c_str(), ',');

  if (xff_address_list.empty()) {
    return EMPTY_STRING;
  }
  return xff_address_list.back();
}

} // Http
