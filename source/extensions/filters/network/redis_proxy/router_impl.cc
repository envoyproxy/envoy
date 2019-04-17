#include "extensions/filters/network/redis_proxy/router_impl.h"

#include "common/common/fmt.h"

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

    auto success = prefix_lookup_table_.add(copy.c_str(),
                                            std::make_shared<Prefix>(Prefix{
                                                route.prefix(),
                                                route.remove_prefix(),
                                                upstreams_.at(route.cluster()),
                                            }),
                                            false);
    if (!success) {
      throw EnvoyException(fmt::format("prefix `{}` already exists.", route.prefix()));
    }
  }
}

ConnPool::InstanceSharedPtr PrefixRoutes::upstreamPool(std::string& key) {
  PrefixPtr value = nullptr;
  if (case_insensitive_) {
    std::string copy(key);
    to_lower_table_.toLowerCase(copy);
    value = prefix_lookup_table_.findLongestPrefix(copy.c_str());
  } else {
    value = prefix_lookup_table_.findLongestPrefix(key.c_str());
  }

  if (value != nullptr) {
    if (value->remove_prefix) {
      key.erase(0, value->prefix.length());
    }
    return value->upstream;
  }

  return catch_all_upstream_;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
