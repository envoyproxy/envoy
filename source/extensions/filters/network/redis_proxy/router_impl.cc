#include "extensions/filters/network/redis_proxy/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MirrorPolicyImpl::MirrorPolicyImpl(const envoy::config::filter::network::redis_proxy::v2::
                                       RedisProxy::PrefixRoutes::Route::RequestMirrorPolicy& config,
                                   const ConnPool::InstanceSharedPtr upstream,
                                   Runtime::Loader& runtime)
    : runtime_key_(config.runtime_fraction().runtime_key()),
      default_value_(config.runtime_fraction().default_value()),
      exclude_read_commands_(config.exclude_read_commands()), upstream_(upstream),
      runtime_(runtime) {}

bool MirrorPolicyImpl::shouldMirror(const std::string& command) const {
  if (!upstream_) {
    return false;
  }

  if (exclude_read_commands_ && Common::Redis::SupportedCommands::writeCommands().find(command) ==
                                    Common::Redis::SupportedCommands::writeCommands().end()) {
    return false;
  }

  if (default_value_.numerator() > 0) {
    return runtime_.snapshot().featureEnabled(runtime_key_, default_value_);
  }

  return true;
}

Prefix::Prefix(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes::Route route,
    Upstreams& upstreams, Runtime::Loader& runtime)
    : prefix_(route.prefix()), remove_prefix_(route.remove_prefix()),
      upstream_(upstreams.at(route.cluster())) {
  for (auto const& mirror_policy : route.request_mirror_policy()) {
    mirror_policies_.emplace_back(std::make_shared<MirrorPolicyImpl>(
        mirror_policy, upstreams.at(mirror_policy.cluster()), runtime));
  }
}

PrefixRoutes::PrefixRoutes(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes& config,
    Upstreams&& upstreams, Runtime::Loader& runtime)
    : case_insensitive_(config.case_insensitive()), upstreams_(std::move(upstreams)),
      catch_all_route_(config.has_catch_all_route()
                           ? std::make_shared<Prefix>(config.catch_all_route(), upstreams_, runtime)
                           : nullptr) {

  for (auto const& route : config.routes()) {
    std::string copy(route.prefix());

    if (case_insensitive_) {
      to_lower_table_.toLowerCase(copy);
    }

    auto success = prefix_lookup_table_.add(
        copy.c_str(), std::make_shared<Prefix>(route, upstreams_, runtime), false);
    if (!success) {
      throw EnvoyException(fmt::format("prefix `{}` already exists.", route.prefix()));
    }
  }
}

RouteSharedPtr PrefixRoutes::upstreamPool(std::string& key) {
  PrefixSharedPtr value = nullptr;
  if (case_insensitive_) {
    std::string copy(key);
    to_lower_table_.toLowerCase(copy);
    value = prefix_lookup_table_.findLongestPrefix(copy.c_str());
  } else {
    value = prefix_lookup_table_.findLongestPrefix(key.c_str());
  }

  if (value != nullptr) {
    if (value->removePrefix()) {
      key.erase(0, value->prefix().length());
    }
    return value;
  }

  return catch_all_route_;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
