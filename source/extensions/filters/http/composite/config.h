#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

#include "common/matcher/matcher.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

/**
 * Config registration for the composite filter. @see NamedHttpFilterConfigFactory.
 */
class CompositeFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::composite::v3::Composite> {
public:
  CompositeFilterFactory() : FactoryBase(HttpFilterNames::get().Composite) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::composite::v3::Composite& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

class CompositeAction
    : public Matcher::ActionBase<envoy::extensions::filters::http::composite::v3::CompositeAction> {

public:
  explicit CompositeAction(Http::FilterFactoryCb cb) : cb_(std::move(cb)) {}

  void createFilters(Http::FilterChainFactoryCallbacks& callbacks) const { cb_(callbacks); }

private:
  const Http::FilterFactoryCb cb_;
};

class CompositeMatchActionFactory : public Matcher::ActionFactory {
public:
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, const std::string& stats_prefix,
                        Server::Configuration::FactoryContext& context) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::composite::v3::CompositeAction&>(
        config, context.messageValidationVisitor());

    auto& factory = Config::Utility::getAndCheckFactoryByType<
        Server::Configuration::NamedHttpFilterConfigFactory>(action_config.typed_config());

    auto message = Config::Utility::translateAnyToFactoryConfig(
        action_config.typed_config(), context.messageValidationVisitor(), factory);

    auto factory_cb = factory.createFilterFactoryFromProto(*message, stats_prefix, context);

    return [factory_cb]() { return std::make_unique<CompositeAction>(factory_cb); };
  }

  std::string name() const override { return "composite-action"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::composite::v3::CompositeAction>();
  }
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
