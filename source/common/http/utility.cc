#include "utility.h"

#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/network/utility.h"

namespace Http {

void Utility::appendXff(HeaderMap& headers, const std::string& remote_address) {
  std::string forwarded_for = headers.get(Headers::get().ForwardedFor);
  if (!forwarded_for.empty()) {
    forwarded_for += ", ";
  }

  forwarded_for += remote_address;
  headers.replaceViaMoveValue(Headers::get().ForwardedFor, std::move(forwarded_for));
}

std::string Utility::createSslRedirectPath(const HeaderMap& headers) {
  ASSERT(headers.has(Headers::get().Host));
  ASSERT(headers.has(Headers::get().Path));
  return fmt::format("https://{}{}", headers.get(Headers::get().Host),
                     headers.get(Headers::get().Path));
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

uint64_t Utility::getResponseStatus(const HeaderMap& headers) {
  uint64_t response_code;
  if (!StringUtil::atoul(headers.get(Headers::get().Status).c_str(), response_code)) {
    throw CodecClientException(":status must be specified and a valid unsigned long");
  }

  return response_code;
}

bool Utility::isInternalRequest(const HeaderMap& headers) {
  std::list<std::reference_wrapper<const std::string>> forwarded_for_headers;
  headers.iterate([&](const LowerCaseString& key, const std::string& value) -> void {
    if (Headers::get().ForwardedFor == key) {
      forwarded_for_headers.emplace_back(value);
    }
  });

  // Only deal with a single x-forwarded-for header.
  if (forwarded_for_headers.size() != 1) {
    return false;
  }

  // An internal request should never travel through multiple proxies.
  const std::string& forwarded_for = forwarded_for_headers.front();
  if (forwarded_for.find(',') != std::string::npos) {
    return false;
  }

  return Network::Utility::isInternalAddress(forwarded_for);
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
    response_headers->addViaMoveValue(Headers::get().ContentLength,
                                      std::to_string(body_text.size()));
    response_headers->addViaCopy(Headers::get().ContentType, Headers::get().ContentTypeValues.Text);
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

} // Http
