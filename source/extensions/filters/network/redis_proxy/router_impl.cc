#include "extensions/filters/network/redis_proxy/router_impl.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MirrorPolicyImpl::MirrorPolicyImpl(const envoy::extensions::filters::network::redis_proxy::v3::
                                       RedisProxy::PrefixRoutes::Route::RequestMirrorPolicy& config,
                                   const ConnPool::InstanceSharedPtr upstream,
                                   Runtime::Loader& runtime)
    : runtime_key_(config.runtime_fraction().runtime_key()),
      default_value_(config.has_runtime_fraction()
                         ? absl::optional<envoy::type::v3::FractionalPercent>(
                               config.runtime_fraction().default_value())
                         : absl::nullopt),
      exclude_read_commands_(config.exclude_read_commands()), upstream_(upstream),
      runtime_(runtime) {}

bool MirrorPolicyImpl::shouldMirror(const std::string& command) const {
  if (!upstream_) {
    return false;
  }

  std::string to_lower_string = absl::AsciiStrToLower(command);

  if (exclude_read_commands_ && Common::Redis::SupportedCommands::isReadCommand(to_lower_string)) {
    return false;
  }

  if (default_value_.has_value()) {
    return runtime_.snapshot().featureEnabled(runtime_key_, default_value_.value());
  }

  return true;
}

Prefix::Prefix(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route
        route,
    Upstreams& upstreams, Runtime::Loader& runtime)
    : prefix_(route.prefix()), remove_prefix_(route.remove_prefix()),
      upstream_(upstreams.at(route.cluster())) {
  for (auto const& mirror_policy : route.request_mirror_policy()) {
    mirror_policies_.emplace_back(std::make_shared<MirrorPolicyImpl>(
        mirror_policy, upstreams.at(mirror_policy.cluster()), runtime));
  }
}

PrefixRoutes::PrefixRoutes(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes& config,
    Upstreams&& upstreams, Runtime::Loader& runtime)
    : case_insensitive_(config.case_insensitive()), upstreams_(std::move(upstreams)),
      catch_all_route_(config.has_catch_all_route()
                           ? std::make_shared<Prefix>(config.catch_all_route(), upstreams_, runtime)
                           : nullptr) {

  for (auto const& route : config.routes()) {
    std::string copy(route.prefix());

    if (case_insensitive_) {
      absl::AsciiStrToLower(&copy);
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
    std::string copy = absl::AsciiStrToLower(key);
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
