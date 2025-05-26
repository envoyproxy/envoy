#pragma once

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/matcher_actions.pb.h"

#include "source/common/common/logger.h"
#include "source/common/http/utility.h"
#include "source/common/matcher/matcher.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using ::envoy::extensions::filters::http::proto_api_scrubber::v3::DescriptorSet;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::RestrictionConfig;
using ::envoy::extensions::filters::http::proto_api_scrubber::v3::Restrictions;
using Http::HttpMatchingData;
using Protobuf::Map;
using xds::type::matcher::v3::HttpAttributesCelMatchInput;
using FilteringMode = ProtoApiScrubberConfig::FilteringMode;
using ProtoApiScrubberRemoveFieldAction =
    ::envoy::extensions::filters::http::proto_api_scrubber::v3::RemoveFieldAction;
using MatchTreeHttpMatchingDataSharedPtr = Matcher::MatchTreeSharedPtr<HttpMatchingData>;
using StringPairToMatchTreeMap =
    absl::flat_hash_map<std::pair<std::string, std::string>, MatchTreeHttpMatchingDataSharedPtr>;
} // namespace

// The config for Proto API Scrubber filter. As a thread-safe class, it should be constructed only
// once and shared among filters for better performance.
class ProtoApiScrubberFilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  static absl::StatusOr<std::shared_ptr<ProtoApiScrubberFilterConfig>>
  create(const ProtoApiScrubberConfig& proto_config,
         Envoy::Server::Configuration::FactoryContext& context);
  MatchTreeHttpMatchingDataSharedPtr getRequestFieldMatcher(std::string method_name,
                                                            std::string field_mask) const;
  MatchTreeHttpMatchingDataSharedPtr getResponseFieldMatcher(std::string method_name,
                                                             std::string field_mask) const;

  FilteringMode filteringMode() const { return filtering_mode_; }

private:
  ProtoApiScrubberFilterConfig();

  absl::Status initialize(const ProtoApiScrubberConfig& proto_config,
                          Envoy::Server::Configuration::FactoryContext& context);
  absl::Status initializeMethodRestrictions(std::string method_name,
                                            StringPairToMatchTreeMap& field_restrictions,
                                            Map<std::string, RestrictionConfig> restrictions,
                                            Server::Configuration::FactoryContext& context);

  absl::Status validateFilteringMode(FilteringMode);
  absl::Status validateMethodName(absl::string_view);
  absl::Status validateFieldMask(absl::string_view);

  FilteringMode filtering_mode_;

  StringPairToMatchTreeMap request_field_restrictions_;
  StringPairToMatchTreeMap response_field_restrictions_;
};

// A class to validate the actions specified on on_match of the unified matcher in the config.
class ActionValidatorVisitor : public Matcher::MatchTreeValidationVisitor<HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<HttpMatchingData>&,
                                          absl::string_view type_url) override {
    if (type_url == TypeUtil::descriptorFullNameToTypeUrl(
                        HttpAttributesCelMatchInput::descriptor()->full_name())) {
      return absl::OkStatus();
    }

    return absl::InvalidArgumentError(
        fmt::format("ProtoApiScrubber filter does not support matching on '{}'", type_url));
  }
};

// Action class for the RemoveFieldAction.
class RemoveFieldAction : public Matcher::ActionBase<ProtoApiScrubberRemoveFieldAction> {};

// ActionFactory for the RemoveFieldAction.
class RemoveFilterActionFactory : public Matcher::ActionFactory<ProtoApiScrubberRemoveFieldAction> {
public:
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message&,
                                                 ProtoApiScrubberRemoveFieldAction&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<RemoveFieldAction>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoApiScrubberRemoveFieldAction>();
  }

  std::string name() const override { return "removeFieldAction"; }
};

using FilterConfigSharedPtr = std::shared_ptr<const ProtoApiScrubberFilterConfig>;

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
