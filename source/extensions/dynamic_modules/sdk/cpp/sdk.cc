#include "sdk.h"

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

DownstreamWatermarkCallbacks::~DownstreamWatermarkCallbacks() = default;

HttpFilterHandle::~HttpFilterHandle() = default;

HttpFilterConfigHandle::~HttpFilterConfigHandle() = default;

namespace {
bool isSameChunks(const std::vector<BufferView>& a, const std::vector<BufferView>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].data() != b[i].data()) {
      return false;
    }
  }
  return true;
}

std::string getBodyContent(BodyBuffer& buffered_body, BodyBuffer& received_body) {
  std::string result;
  const auto buffered_chunks = buffered_body.getChunks();
  for (const auto& chunk : buffered_chunks) {
    result.append(chunk.data(), chunk.size());
  }
  const auto received_chunks = received_body.getChunks();
  // Because of the complex buffering logic in Envoy, it's possible that the latest received body
  // is the same as the buffered body. This happens when a previous filter returns StopAndBuffer,
  // and then this filter is called again with the buffered body as the received body.
  // TODO(wbpcode): optimize this by adding a new ABI to directly check it.
  if (!isSameChunks(buffered_chunks, received_chunks)) {
    for (const auto& chunk : received_chunks) {
      result.append(chunk.data(), chunk.size());
    }
  }
  return result;
}
} // namespace

namespace Utility {
std::string readWholeRequestBody(HttpFilterHandle& handle) {
  return getBodyContent(handle.bufferedRequestBody(), handle.receivedRequestBody());
}

std::string readWholeResponseBody(HttpFilterHandle& handle) {
  return getBodyContent(handle.bufferedResponseBody(), handle.receivedResponseBody());
}
} // namespace Utility

HttpFilterConfigFactoryRegister::HttpFilterConfigFactoryRegister(absl::string_view name,
                                                                 HttpFilterConfigFactoryPtr factory)
    : name_(name) {
  auto r = HttpFilterConfigFactoryRegistry::getMutableRegistry().emplace(name_, std::move(factory));
  if (!r.second) {
    std::string error_msg = std::format("Factory with the same name {} already registered", name_);
    std::cerr << error_msg << std::endl;
    assert((void("Duplicate factory registration"), r.second));
  }
}

HttpFilterConfigFactoryRegister::~HttpFilterConfigFactoryRegister() {
  HttpFilterConfigFactoryRegistry::getMutableRegistry().erase(name_);
}

absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getMutableRegistry() {
  static absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr> registry;
  return registry;
}

const absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>&
HttpFilterConfigFactoryRegistry::getRegistry() {
  return getMutableRegistry();
};

} // namespace DynamicModules
} // namespace Envoy
