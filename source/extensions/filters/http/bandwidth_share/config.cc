#include <string>

#include "envoy/extensions/filters/http/bandwidth_share/v3/bandwidth_share.pb.h"
#include "envoy/extensions/filters/http/bandwidth_share/v3/bandwidth_share.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/actions/string_returning_action.h"
#include "source/common/matcher/matcher.h"
#include "source/common/matcher/validation_visitor.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/bandwidth_share/filter.h"
#include "source/extensions/filters/http/bandwidth_share/filter_config.h"
#include "source/extensions/filters/http/common/factory_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

using ProtoConfig = envoy::extensions::filters::http::bandwidth_share::v3::BandwidthShare;
using Matcher::Actions::StringReturningActionFactoryContext;

namespace {

const xds::type::matcher::v3::Matcher& blankTenantSelector() {
  static xds::type::matcher::v3::Matcher matcher = [] {
    xds::type::matcher::v3::Matcher matcher;
    auto action = matcher.mutable_on_no_match()->mutable_action();
    action->set_name("empty_tenant");
    Protobuf::StringValue str;
    str.set_value("");
    action->mutable_typed_config()->PackFrom(str);
    return matcher;
  }();
  return matcher;
}

class MatcherDataInputValidator
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view /*type_url*/) override {
    // All correctly typed inputs are fine, and actions are also validated by type.
    return absl::OkStatus();
  }
};

absl::StatusOr<std::shared_ptr<FilterConfig>>
protoToSharedState(const ProtoConfig& config,
                   Server::Configuration::ServerFactoryContext& context) {
  if (!config.response_trailer_prefix().empty() && !config.enable_response_trailers()) {
    return absl::InvalidArgumentError(
        "enable_response_trailers must be true if response_trailer_prefix is set");
  }
  HttpMatchTreePtr tenant_name_selector;
  // MatchTreeFactory::create doesn't have an exception-free variant.
  TRY_NEEDS_AUDIT {
    MatcherDataInputValidator matcher_validator;
    StringReturningActionFactoryContext string_context{context};
    Matcher::MatchTreeFactory<Http::HttpMatchingData, StringReturningActionFactoryContext>
        matcher_factory(string_context, context, matcher_validator);
    auto matcher_factory_cb = matcher_factory.create(
        config.has_tenant_name_selector() ? config.tenant_name_selector() : blankTenantSelector());
    tenant_name_selector = matcher_factory_cb();
  }
  END_TRY CATCH(const EnvoyException& e, {
    return absl::InvalidArgumentError(absl::StrCat("invalid tenant_name_selector:\n", e.what()));
  });
  auto bucket_singleton =
      TokenBucketSingleton::get(context.singletonManager(), context.timeSource(), context.scope());
  std::chrono::milliseconds fill_interval{PROTOBUF_GET_MS_OR_DEFAULT(config, fill_interval, 50)};
  if (config.has_request_limit()) {
    RETURN_IF_NOT_OK(bucket_singleton->setBucket(
        config.request_limit().bucket_id(),
        Runtime::UInt32(config.request_limit().kbps(), context.runtime()), fill_interval));
  }
  if (config.has_response_limit()) {
    RETURN_IF_NOT_OK(bucket_singleton->setBucket(
        config.response_limit().bucket_id(),
        Runtime::UInt32(config.response_limit().kbps(), context.runtime()), fill_interval));
  }
  absl::flat_hash_map<std::string, FilterConfig::TenantConfig> tenant_configs;
  for (const auto& pair : config.tenant_configs()) {
    tenant_configs.try_emplace(pair.first, std::max(pair.second.weight(), 1U),
                               pair.second.include_stats_tag());
  }
  FilterConfig::TenantConfig default_tenant_config{
      std::max(config.default_tenant_config().weight(), 1U),
      config.default_tenant_config().include_stats_tag()};
  return std::make_shared<FilterConfig>(
      context, bucket_singleton,
      config.has_request_limit()
          ? absl::optional<absl::string_view>(config.request_limit().bucket_id())
          : absl::nullopt,
      config.has_response_limit()
          ? absl::optional<absl::string_view>(config.response_limit().bucket_id())
          : absl::nullopt,
      config.enable_response_trailers()
          ? absl::optional<absl::string_view>(config.response_trailer_prefix())
          : absl::nullopt,
      std::move(tenant_name_selector), std::move(tenant_configs), std::move(default_tenant_config));
}
} // namespace

class BandwidthShareFilterFactory : public Common::DualFactoryBase<ProtoConfig, ProtoConfig> {
public:
  BandwidthShareFilterFactory();

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ProtoConfig&, const std::string& stats_prefix, DualInfo,
                                    Server::Configuration::ServerFactoryContext&) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const ProtoConfig&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

BandwidthShareFilterFactory::BandwidthShareFilterFactory()
    : DualFactoryBase(BandwidthShare::filterName()) {}

absl::StatusOr<Http::FilterFactoryCb>
BandwidthShareFilterFactory::createFilterFactoryFromProtoTyped(
    const ProtoConfig& proto_config, const std::string&, DualInfo,
    Server::Configuration::ServerFactoryContext& context) {
  auto shared_state = protoToSharedState(proto_config, context);
  if (!shared_state.ok()) {
    return shared_state.status();
  }
  return
      [ss = std::move(shared_state).value()](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(std::make_unique<BandwidthShare>(ss));
      };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
BandwidthShareFilterFactory::createRouteSpecificFilterConfigTyped(
    const ProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  return protoToSharedState(proto_config, context);
}

REGISTER_FACTORY(BandwidthShareFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
