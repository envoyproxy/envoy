#include "source/extensions/filters/network/redis_proxy/hash_slot_router_impl.h"

#include <cstddef>
#include <cstdint>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/network/redis_proxy/mirror_policy_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

HashSlot::HashSlot(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes::Route
        route,
    Upstreams& upstreams, Runtime::Loader& runtime)
    : upstream_(upstreams.at(route.cluster())) {
  for (auto const& mirror_policy : route.request_mirror_policy()) {
    mirror_policies_.emplace_back(std::make_shared<MirrorPolicyImpl>(
        mirror_policy, upstreams.at(mirror_policy.cluster()), runtime));
  }
}

HashSlotRoutes::HashSlotRoutes(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes& config,
    Upstreams&& upstreams, Runtime::Loader& runtime)
    : upstreams_(std::move(upstreams)),
      catch_all_route_(
          config.has_catch_all_route()
              ? std::make_shared<HashSlot>(config.catch_all_route(), upstreams_, runtime)
              : nullptr) {

  for (auto const& route : config.routes()) {
    HashSlotSharedPtr slot_upstream = std::make_shared<HashSlot>(route, upstreams_, runtime);

    for (auto const& slot : route.slot_ranges()) {
      for (uint32_t i = slot.start(); i <= slot.end(); i++) {
        if (slots_assignment_[i] != nullptr) {
          throw EnvoyException(fmt::format("slot `{}` is already assigned.", i));
        }
        slots_assignment_[i] = slot_upstream;
      }
    }
  }

  for (uint32_t i = 0; i < MaxHashSlot; i++) {
    if (slots_assignment_[i] == nullptr) {
      if (catch_all_route_ == nullptr) {
        throw EnvoyException(
            fmt::format("slot `{}` is not assigned and catch_all_route is not configured", i));
      }
      slots_assignment_[i] = catch_all_route_;
    }
  }
}

RouteSharedPtr HashSlotRoutes::upstreamPool(std::string& key) {
  HashSlotSharedPtr value = nullptr;

  value = slots_assignment_.at(HashUtil::xxHash64(key) % MaxHashSlot);

  return value;
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
