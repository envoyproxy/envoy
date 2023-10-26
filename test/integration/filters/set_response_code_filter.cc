#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/set_response_code_filter_config.pb.h"
#include "test/integration/filters/set_response_code_filter_config.pb.validate.h"

#include "absl/strings/match.h"

namespace Envoy {

// A test filter that responds directly with a code on a prefix match.
class SetResponseCodeFilterConfig {
public:
  SetResponseCodeFilterConfig(const std::string& prefix, uint32_t code, const std::string& body,
                              Server::Configuration::FactoryContextBase& context)
      : prefix_(prefix), code_(code), body_(body), tls_slot_(context.threadLocal()) {}

  const std::string prefix_;
  const uint32_t code_;
  const std::string body_;
  // Allocate a slot to validate that it is destroyed on a main thread only.
  ThreadLocal::TypedSlot<> tls_slot_;
};

class SetResponseCodeFilterRouteSpecificConfig : public Envoy::Router::RouteSpecificFilterConfig {
public:
  SetResponseCodeFilterRouteSpecificConfig(const std::string& prefix, uint32_t code,
                                           const std::string& body,
                                           Server::Configuration::FactoryContextBase& context)
      : prefix_(prefix), code_(code), body_(body), tls_slot_(context.threadLocal()) {}

  const std::string prefix_;
  const uint32_t code_;
  const std::string body_;
  // Allocate a slot to validate that it is destroyed on a main thread only.
  ThreadLocal::TypedSlot<> tls_slot_;
};

class SetResponseCodeFilter : public Http::PassThroughFilter {
public:
  SetResponseCodeFilter(std::shared_ptr<SetResponseCodeFilterConfig> config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    const auto* per_route_config = Envoy::Http::Utility::resolveMostSpecificPerFilterConfig<
        SetResponseCodeFilterRouteSpecificConfig>(decoder_callbacks_);

    std::string prefix;
    uint32_t code;
    std::string body;
    // Route level config takes precedence over filter level config, if present.
    if (per_route_config != nullptr) {
      prefix = per_route_config->prefix_;
      code = per_route_config->code_;
      body = per_route_config->body_;
    } else {
      prefix = config_->prefix_;
      code = config_->code_;
      body = config_->body_;
    }

    if (absl::StartsWith(headers.Path()->value().getStringView(), prefix)) {
      decoder_callbacks_->sendLocalReply(static_cast<Http::Code>(code), body, nullptr,
                                         absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::shared_ptr<SetResponseCodeFilterConfig> config_;
};

class SetResponseCodeFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          test::integration::filters::SetResponseCodeFilterConfig,
          test::integration::filters::SetResponseCodePerRouteFilterConfig> {
public:
  SetResponseCodeFilterFactory() : FactoryBase("set-response-code-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::SetResponseCodeFilterConfig& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override {
    auto filter_config = std::make_shared<SetResponseCodeFilterConfig>(
        proto_config.prefix(), proto_config.code(), proto_config.body(), context);
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<SetResponseCodeFilter>(filter_config));
    };
  }
  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const test::integration::filters::SetResponseCodeFilterConfig& proto_config,
      const std::string&, Server::Configuration::ServerFactoryContext& context) override {
    auto filter_config = std::make_shared<SetResponseCodeFilterConfig>(
        proto_config.prefix(), proto_config.code(), proto_config.body(), context);
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<SetResponseCodeFilter>(filter_config));
    };
  }

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const test::integration::filters::SetResponseCodePerRouteFilterConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor&) override {
    return std::make_shared<const SetResponseCodeFilterRouteSpecificConfig>(
        proto_config.prefix(), proto_config.code(), proto_config.body(), context);
  }
};

REGISTER_FACTORY(SetResponseCodeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
