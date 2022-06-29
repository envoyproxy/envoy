#include "source/extensions/filters/http/custom_response/config.h"

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

struct CustomResponseNameAction : public Matcher::ActionBase<ProtobufWkt::StringValue> {
  explicit CustomResponseNameAction(ResponseSharedPtr response) : response_(response) {}
  const ResponseSharedPtr response_;
};

using CustomResponseActionFactoryContext =
    absl::flat_hash_map<absl::string_view, ResponseSharedPtr>;

class CustomResponseNameActionFactory
    : public Matcher::ActionFactory<CustomResponseActionFactoryContext>,
      Logger::Loggable<Logger::Id::config> {
public:
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                                 CustomResponseActionFactoryContext& responses,
                                                 ProtobufMessage::ValidationVisitor&) override {
    ResponseSharedPtr response = nullptr;
    const auto& name = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    const auto response_match = responses.find(name.value());
    if (response_match != responses.end()) {
      response = response_match->second;
    } else {
      ENVOY_LOG(debug, "matcher API points to an absent custom response '{}'", name.value());
    }
    return [response = std::move(response)]() {
      return std::make_unique<CustomResponseNameAction>(response);
    };
  }
  std::string name() const override { return "custom_response_name"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
};

REGISTER_FACTORY(CustomResponseNameActionFactory,
                 Matcher::ActionFactory<CustomResponseActionFactoryContext>);

class CustomResponseNameActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace

Http::FilterFactoryCb CustomFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto config_ptr =
      std::make_shared<envoy::extensions::filters::http::custom_response::v3::CustomResponse>(
          config);
  return [config_ptr, stats_prefix,
          &context](Http::FilterChainFactoryCallbacks& callbacks) mutable -> void {
    callbacks.addStreamFilter(
        std::make_shared<CustomResponseFilter>(config_ptr, context, stats_prefix));
  };
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Server::Configuration::FactoryContext& context) {
  for (const auto& source : config.custom_responses()) {
    auto source_ptr = std::make_shared<Response>(source, context);
    // TODO check for empty name
    responses_.emplace(source_ptr->name(), std::move(source_ptr));
    // TODO throw if repeated names found.
  }
  if (config.has_custom_response_matcher()) {
    CustomResponseNameActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Http::HttpMatchingData, CustomResponseActionFactoryContext> factory(
        responses_, context.getServerFactoryContext(), validation_visitor);
    matcher_ = factory.create(config.custom_response_matcher())();
  } else {
    throw EnvoyException("matcher can not be unset");
  }
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
