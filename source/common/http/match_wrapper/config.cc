#include "common/http/match_wrapper/config.h"

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "common/matcher/matcher.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchWrapper {

namespace {
struct DelegatingFactoryCallbacks : public Envoy::Http::FilterChainFactoryCallbacks {
  DelegatingFactoryCallbacks(Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks,
                             Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree)
      : delegated_callbacks_(delegated_callbacks), match_tree_(std::move(match_tree)) {}

  void addStreamDecoderFilter(Envoy::Http::StreamDecoderFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter), match_tree_);
  }
  void addStreamDecoderFilter(
      Envoy::Http::StreamDecoderFilterSharedPtr filter,
      Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamEncoderFilter(Envoy::Http::StreamEncoderFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter), match_tree_);
  }
  void addStreamEncoderFilter(
      Envoy::Http::StreamEncoderFilterSharedPtr filter,
      Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamFilter(std::move(filter), match_tree_);
  }
  void
  addStreamFilter(Envoy::Http::StreamFilterSharedPtr filter,
                  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamFilter(std::move(filter), std::move(match_tree));
  }
  void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override {
    delegated_callbacks_.addAccessLogHandler(std::move(handler));
  }

  Envoy::Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree_;
};
} // namespace

Envoy::Http::FilterFactoryCb MatchWrapperConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {

  if (proto_config.has_extension_config()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            proto_config.extension_config());

    auto message = factory.createEmptyConfigProto();
    proto_config.extension_config().typed_config();
    Config::Utility::translateOpaqueConfig(proto_config.extension_config().typed_config(),
                                           ProtobufWkt::Struct(),
                                           context.messageValidationVisitor(), *message);
    auto filter_factory = factory.createFilterFactoryFromProto(*message, prefix, context);

    auto match_tree =
        Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData>(context.messageValidationVisitor())
            .create(proto_config.matcher());

    return
        [filter_factory, match_tree](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
          DelegatingFactoryCallbacks delegated_callbacks(callbacks, match_tree);

          return filter_factory(delegated_callbacks);
        };
  }

  return [](Envoy::Http::FilterChainFactoryCallbacks&) -> void {};
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MatchWrapperConfig, Server::Configuration::NamedHttpFilterConfigFactory){"blah"};

} // namespace MatchWrapper
} // namespace Http
} // namespace Common
} // namespace Envoy
