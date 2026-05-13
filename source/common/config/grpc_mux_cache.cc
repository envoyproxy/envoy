#include "source/common/config/grpc_mux_cache.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Config {

std::shared_ptr<GrpcMux> GrpcMuxCache::getOrCreateMux(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    std::function<std::shared_ptr<GrpcMux>()> mux_creator) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  GrpcMuxKey key(config, type_url);
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    if (auto mux = it->second.lock()) {
      return mux;
    }
  }
  auto mux = mux_creator();
  cache_[key] = mux;
  return mux;
}

void GrpcMuxCache::clear() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  cache_.clear();
}

} // namespace Config
} // namespace Envoy
