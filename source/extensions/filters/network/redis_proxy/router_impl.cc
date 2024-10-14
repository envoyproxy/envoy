#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"

#include "absl/strings/str_replace.h"

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
    : prefix_(route.prefix()), key_formatter_(route.key_formatter()),
      remove_prefix_(route.remove_prefix()), upstream_(upstreams.at(route.cluster())) {
  for (auto const& mirror_policy : route.request_mirror_policy()) {
    mirror_policies_.emplace_back(std::make_shared<MirrorPolicyImpl>(
        mirror_policy, upstreams.at(mirror_policy.cluster()), runtime));
  }
  if (route.has_read_command_policy()) {
    read_upstream_ = upstreams.at(route.read_command_policy().cluster());
  }
}

ConnPool::InstanceSharedPtr Prefix::upstream(const std::string& command) const {

  if (read_upstream_) {
    std::string to_lower_string = absl::AsciiStrToLower(command);
    if (Common::Redis::SupportedCommands::isReadCommand(to_lower_string)) {
      return read_upstream_;
    }
  }

  return upstream_;
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

RouteSharedPtr PrefixRoutes::upstreamPool(std::string& key,
                                          const StreamInfo::StreamInfo& stream_info) {
  PrefixSharedPtr value = nullptr;
  if (case_insensitive_) {
    std::string copy = absl::AsciiStrToLower(key);
    value = prefix_lookup_table_.findLongestPrefix(copy);
  } else {
    value = prefix_lookup_table_.findLongestPrefix(key);
  }

  if (value == nullptr) {
    // prefix route not found, default to catch all route.
    value = catch_all_route_;
    // prefix route not found, check if catch_all_route is defined to fallback to.
    if (catch_all_route_ != nullptr) {
      value = catch_all_route_;
    } else {
      // no route found.
      return value;
    }
  }

  if (value->removePrefix()) {
    key.erase(0, value->prefix().length());
  }
  if (!value->keyFormatter().empty()) {
    formatKey(key, value->keyFormatter(), stream_info);
  }
  return value;
}

void PrefixRoutes::formatKey(std::string& key, std::string redis_key_formatter,
                             const StreamInfo::StreamInfo& stream_info) {
  // If key_formatter defines %KEY% command, then do a direct string replacement.
  // TODO(deveshkandpal24121990) - Possibly define a RedisKeyFormatter as a SubstitutionFormatter.
  // There is a possibility that key might have a '%' character in it, which will be incorrectly
  // processed by SubstitutionFormatter. To avoid that, replace the '%KEY%' command with
  // '~REPLACED_KEY~` place holder. After SubstitutionFormatter is done, replace the
  // '~REPLACED_KEY~' with the actual key.
  if (redis_key_formatter.find(redis_key_formatter_command_) != std::string::npos) {
    redis_key_formatter = absl::StrReplaceAll(
        redis_key_formatter, {{redis_key_formatter_command_, redis_key_to_be_replaced_}});
  }
  auto providers = *Formatter::SubstitutionFormatParser::parse(redis_key_formatter);
  std::string formatted_key;
  for (Formatter::FormatterProviderPtr& provider : providers) {
    auto provider_formatted_key = provider->formatValueWithContext({}, stream_info);
    if (provider_formatted_key.has_string_value()) {
      formatted_key = formatted_key + provider_formatted_key.string_value();
    }
  }
  if (formatted_key.find(redis_key_to_be_replaced_) != std::string::npos) {
    formatted_key = absl::StrReplaceAll(formatted_key, {{redis_key_to_be_replaced_, key}});
  }
  if (!formatted_key.empty()) {
    key = formatted_key;
  }
  ENVOY_LOG(trace, "formatted key is {}", key);
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
