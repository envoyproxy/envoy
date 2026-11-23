#include "sdk.h"

#include <algorithm>
#include <array>
#include <format>
#include <iostream>
#include <string_view>

namespace Envoy {
namespace DynamicModules {

HttpFilterConfigFactory::~HttpFilterConfigFactory() = default;

HttpFilterFactory::~HttpFilterFactory() = default;

HttpFilter::~HttpFilter() = default;

BodyBuffer::~BodyBuffer() = default;

HeaderMap::~HeaderMap() = default;

HttpCalloutCallback::~HttpCalloutCallback() = default;

HttpStreamCallback::~HttpStreamCallback() = default;

RouteSpecificConfig::~RouteSpecificConfig() = default;

Scheduler::~Scheduler() = default;

LoadShedPoint::~LoadShedPoint() = default;

DownstreamWatermarkCallbacks::~DownstreamWatermarkCallbacks() = default;

HttpFilterHandle::~HttpFilterHandle() = default;

HttpFilterConfigHandle::~HttpFilterConfigHandle() = default;

CommonHandle::~CommonHandle() = default;

namespace Utility {

std::string getBodyContent(BodyBuffer& buffered, BodyBuffer& received, bool is_buffered) {
  const size_t total_size = buffered.getSize() + (is_buffered ? 0 : received.getSize());
  std::string result;
  result.reserve(total_size);
  for (const auto& chunk : buffered.getChunks()) {
    result.append(chunk.data(), chunk.size());
  }

  // If the received body is the same as the buffered body (a previous filter did StopAndBuffer
  // and resumed), skip the received body to avoid duplicating data.
  if (is_buffered) {
    return result;
  }

  for (const auto& chunk : received.getChunks()) {
    result.append(chunk.data(), chunk.size());
  }
  return result;
}

std::string readWholeRequestBody(HttpFilterHandle& handle) {
  return getBodyContent(handle.bufferedRequestBody(), handle.receivedRequestBody(),
                        handle.receivedBufferedRequestBody());
}

std::string readWholeResponseBody(HttpFilterHandle& handle) {
  return getBodyContent(handle.bufferedResponseBody(), handle.receivedResponseBody(),
                        handle.receivedBufferedResponseBody());
}

// The bytes of an HTTP token (RFC 9110), which is what a header name is.
constexpr std::array<bool, 256> TokenChars = [] {
  std::array<bool, 256> table{};
  for (int c = 0; c < 256; ++c) {
    table[c] =
        (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos;
  }
  return table;
}();

bool isValidHeaderName(std::string_view name) {
  if (name.starts_with(':')) {
    name.remove_prefix(1);
  }
  return !name.empty() &&
         std::all_of(name.begin(), name.end(), [](unsigned char c) { return TokenChars[c]; });
}

bool isValidHeaderValue(std::string_view value) {
  // c ^ 0x16 maps tab to 0x1f and every other control character below it, and DEL is the largest
  // signed char. A min and a max have no early exit, so the compiler can vectorize them, and
  // reading the last 16, 8 or 4 bytes a second time keeps short values off the byte-at-a-time path.
  unsigned char min = 0xff;
  signed char max = 0;
  const auto scan = [&min, &max](std::string_view bytes) {
    for (const unsigned char c : bytes) {
      min = std::min<unsigned char>(min, c ^ 0x16);
      max = std::max(max, static_cast<signed char>(c));
    }
  };
  const size_t size = value.size();
  if (size >= 16) {
    scan(value.substr(0, size & ~size_t{15}));
    scan(value.substr(size - 16));
  } else if (size >= 8) {
    scan(value.substr(0, 8));
    scan(value.substr(size - 8));
  } else if (size >= 4) {
    scan(value.substr(0, 4));
    scan(value.substr(size - 4));
  } else {
    scan(value);
  }
  return min >= 0x1f && max != 0x7f;
}
} // namespace Utility

HttpFilterConfigFactoryRegister::HttpFilterConfigFactoryRegister(std::string_view name,
                                                                 HttpFilterConfigFactoryPtr factory)
    : name_(name) {
  // The register will have longer lifetime than the related factory entry in the registry,
  // so it's safe to store the name as string_view in the registry.
  auto r = HttpFilterConfigFactoryRegistry::getMutableRegistry().emplace(std::string_view(name_),
                                                                         std::move(factory));
  if (!r.second) {
    std::string error_msg = std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

HttpFilterConfigFactoryRegister::~HttpFilterConfigFactoryRegister() {
  HttpFilterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

std::map<std::string_view, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getMutableRegistry() {
  static std::map<std::string_view, HttpFilterConfigFactoryPtr> registry;
  return registry;
}

const std::map<std::string_view, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
};

} // namespace DynamicModules
} // namespace Envoy
