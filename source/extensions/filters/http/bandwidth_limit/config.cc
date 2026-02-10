#include "source/extensions/filters/http/bandwidth_limit/config.h"

#include <string>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"
#include "source/extensions/filters/http/bandwidth_limit/bucket_selectors.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

using Common::StreamRateLimiter;

namespace {
absl::Status populateNamedBucketConfigurations(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context) {
  if (proto_config.named_bucket_configurations_size() == 0) {
    return absl::OkStatus();
  }
  std::shared_ptr<NamedBucketSingleton> named_bucket_singleton = NamedBucketSingleton::get(context);
  std::chrono::milliseconds default_fill_interval =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
          proto_config, fill_interval, StreamRateLimiter::DefaultFillInterval.count()));
  for (const auto& bucket : proto_config.named_bucket_configurations()) {
    std::chrono::milliseconds fill_interval =
        bucket.has_fill_interval() ? std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
                                         bucket.fill_interval()))
                                   : default_fill_interval;
    auto new_bucket = std::make_unique<BucketAndStats>(
        bucket.name(), context.timeSource(), context.scope(), bucket.limit_kbps(), fill_interval);
    uint64_t max_tokens = StreamRateLimiter::kiloBytesToBytes(bucket.limit_kbps());
    uint64_t initial_tokens = max_tokens * fill_interval.count() / 1000;
    new_bucket->bucket()->maybeReset(initial_tokens);
    RETURN_IF_NOT_OK(named_bucket_singleton->setBucket(bucket.name(), std::move(new_bucket)));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<NamedBucketSelector>> maybeNamedBucketSelector(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context) {
  if (!proto_config.has_named_bucket_selector()) {
    return nullptr;
  }
  std::shared_ptr<NamedBucketSingleton> named_bucket_singleton = NamedBucketSingleton::get(context);

  int selected_options =
      (proto_config.named_bucket_selector().explicit_bucket().empty() ? 0 : 1) +
      (proto_config.named_bucket_selector().has_client_cn_with_default() ? 1 : 0);
  if (selected_options != 1) {
    return absl::InvalidArgumentError("precisely one of client_cn_with_default and explicit_bucket "
                                      "must be set on NamedBucketSelector");
  }
  BucketCreationFn bucket_creation_fn;
  if (proto_config.named_bucket_selector().create_bucket_if_not_existing()) {
    if (!proto_config.has_limit_kbps()) {
      return absl::InvalidArgumentError(
          "limit_kbps must be set if create_bucket_if_not_existing is set");
    }
    bucket_creation_fn =
        [&time_source = context.timeSource(), &scope = context.scope(),
         default_limit_kbps = proto_config.limit_kbps().value(),
         default_fill_interval = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
             proto_config, fill_interval, StreamRateLimiter::DefaultFillInterval.count()))](
            absl::string_view name) {
          return std::make_unique<BucketAndStats>(name, time_source, scope, default_limit_kbps,
                                                  default_fill_interval);
        };
  }
  if (!proto_config.named_bucket_selector().explicit_bucket().empty()) {
    return std::make_shared<FixedNamedBucketSelector>(
        std::move(named_bucket_singleton), std::move(bucket_creation_fn),
        proto_config.named_bucket_selector().explicit_bucket());
  }
  // Remaining option is client_cn_with_default.
  return std::make_shared<ClientCnBucketSelector>(
      std::move(named_bucket_singleton), std::move(bucket_creation_fn),
      proto_config.named_bucket_selector().client_cn_with_default().default_bucket(),
      proto_config.named_bucket_selector().client_cn_with_default().name_template());
}
} // namespace

absl::StatusOr<Http::FilterFactoryCb> BandwidthLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();
  absl::StatusOr<std::shared_ptr<NamedBucketSelector>> bucket_selector =
      maybeNamedBucketSelector(proto_config, server_context);
  if (!bucket_selector.ok()) {
    return bucket_selector.status();
  }
  RETURN_IF_NOT_OK(populateNamedBucketConfigurations(proto_config, server_context));
  absl::StatusOr<FilterConfigSharedPtr> filter_config =
      FilterConfig::create(proto_config, std::move(bucket_selector.value()), context.scope(),
                           server_context.runtime(), server_context.timeSource());
  RETURN_IF_NOT_OK_REF(filter_config.status());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<BandwidthLimiter>(*filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
BandwidthLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  absl::StatusOr<std::shared_ptr<NamedBucketSelector>> bucket_selector =
      maybeNamedBucketSelector(proto_config, context);
  if (!bucket_selector.ok()) {
    return bucket_selector.status();
  }
  RETURN_IF_NOT_OK(populateNamedBucketConfigurations(proto_config, context));
  return FilterConfig::create(proto_config, bucket_selector.value(), context.scope(),
                              context.runtime(), context.timeSource(), true);
}

/**
 * Static registration for the bandwidth limit filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(BandwidthLimitFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.bandwidth_limit");

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
