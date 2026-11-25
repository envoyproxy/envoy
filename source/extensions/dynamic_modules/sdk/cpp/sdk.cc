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
  // c ^ 0x16 maps tab to 0x1f and every other control character below it. Bitwise operators and
  // no early exit keep the loop free of branches, so clang, GCC 16 and MSVC x64 vectorize it.
  // GCC 13 to 15 keep it scalar.
  bool valid = true;
  for (const unsigned char c : value) {
    valid &= ((c ^ 0x16) >= 0x1f) & (c != 0x7f);
  }
  return valid;
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
