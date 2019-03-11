#include "extensions/filters/network/redis_proxy/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

PrefixRoutes::PrefixRoutes(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes& config,
    Upstreams&& upstreams)
    : case_insensitive_(config.case_insensitive()), upstreams_(std::move(upstreams)),
      catch_all_upstream_(config.catch_all_cluster().empty()
                              ? nullptr
                              : upstreams_.at(config.catch_all_cluster())) {

  for (auto const& route : config.routes()) {
    std::string copy(route.prefix());

    if (case_insensitive_) {
      to_lower_table_.toLowerCase(copy);
    }

    prefix_lookup_table_.add(copy.c_str(), std::make_shared<Prefix>(Prefix{
                                               route.prefix(),
                                               route.remove_prefix(),
                                               upstreams_.at(route.cluster()),
                                           }));
  }
}

Common::Redis::Client::PoolRequest*
PrefixRoutes::makeRequest(const std::string& key, const Common::Redis::RespValue& request,
                          Common::Redis::Client::PoolCallbacks& callbacks) {

  std::string copy(key);
  if (case_insensitive_) {
    to_lower_table_.toLowerCase(copy);
  }

  auto value = prefix_lookup_table_.findPrefix(copy.c_str());

  if (value != nullptr) {
    // TODO: remove_prefix
    value->upstream->makeRequest(key, request, callbacks);
  } else if (catch_all_upstream_ != nullptr) {
    catch_all_upstream_.value()->makeRequest(key, request, callbacks);
  }

  return nullptr;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
