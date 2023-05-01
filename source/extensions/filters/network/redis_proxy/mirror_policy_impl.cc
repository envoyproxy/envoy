#include "source/extensions/filters/network/redis_proxy/mirror_policy_impl.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

MirrorPolicyImpl::MirrorPolicyImpl(const envoy::extensions::filters::network::redis_proxy::v3::
                                       RedisProxy::RequestMirrorPolicy& config,
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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

