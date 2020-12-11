#include "common/http/match_wrapper/config.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "common/matcher/matcher.h"
#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace MatchWrapper {

namespace {
struct DelegatingFactoryCallbacks : public Http::FilterChainFactoryCallbacks {
  DelegatingFactoryCallbacks(Http::FilterChainFactoryCallbacks& delegated_callbacks,
                             Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree)
      : delegated_callbacks_(delegated_callbacks), match_tree_(std::move(match_tree)) {}

  void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter), match_tree_);
  }
  void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter,
                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamDecoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter), match_tree_);
  }
  void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter,
                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamEncoderFilter(std::move(filter), std::move(match_tree));
  }
  void addStreamFilter(Http::StreamFilterSharedPtr filter) override {
    delegated_callbacks_.addStreamFilter(std::move(filter), match_tree_);
  }
  void addStreamFilter(Http::StreamFilterSharedPtr filter,
                       Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree) override {
    delegated_callbacks_.addStreamFilter(std::move(filter), std::move(match_tree));
  }
  void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override {
    delegated_callbacks_.addAccessLogHandler(std::move(handler));
  }

  Http::FilterChainFactoryCallbacks& delegated_callbacks_;
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_;
};
} // namespace

Http::FilterFactoryCb MatchWrapperConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
    const std::string& prefix, Server::Configuration::FactoryContext& context) {

  if (proto_config.has_extension_config()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            proto_config.extension_config());

    auto message = factory.createEmptyConfigProto();
    proto_config.extension_config().typed_config();
    Config::Utility::translateOpaqueConfig(proto_config.extension_config().typed_config(), ProtobufWkt::Struct(),
                                           context.messageValidationVisitor(), *message);
    auto filter_factory =
        factory.createFilterFactoryFromProto(*message, prefix, context);

    auto match_tree =
        Matcher::MatchTreeFactory<Http::HttpMatchingData>(context.messageValidationVisitor())
            .create(proto_config.matcher());

    return [filter_factory, match_tree](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      DelegatingFactoryCallbacks delegated_callbacks(callbacks, match_tree);

      return filter_factory(delegated_callbacks);
    };
  }

  return [](Http::FilterChainFactoryCallbacks&) -> void {};
}

/**
 * Static registration for the Lua filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MatchWrapperConfig, Server::Configuration::NamedHttpFilterConfigFactory){"blah"};

} // namespace MatchWrapper
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
