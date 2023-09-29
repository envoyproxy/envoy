#include "source/extensions/filters/common/set_filter_state/filter_config.h"

#include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {

std::vector<Rule>
Config::parse(const Protobuf::RepeatedPtrField<
                  envoy::extensions::filters::common::set_filter_state::v3::Rule>& proto_rules,
              Server::Configuration::CommonFactoryContext& context) const {
  std::vector<Rule> rules;
  rules.reserve(proto_rules.size());
  for (const auto& proto_rule : proto_rules) {
    Rule rule;
    rule.key_ = proto_rule.key();
    rule.factory_ =
        Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(rule.key_);
    if (rule.factory_ == nullptr) {
      throw EnvoyException(fmt::format("'{}' does not have an object factory", rule.key_));
    }
    rule.state_type_ = proto_rule.read_only() ? StateType::ReadOnly : StateType::Mutable;
    switch (proto_rule.shared_with_upstream()) {
    case envoy::extensions::filters::common::set_filter_state::v3::Rule::ONCE:
      rule.stream_sharing_ = StreamSharing::SharedWithUpstreamConnectionOnce;
      break;
    case envoy::extensions::filters::common::set_filter_state::v3::Rule::TRANSITIVE:
      rule.stream_sharing_ = StreamSharing::SharedWithUpstreamConnection;
      break;
    default:
      rule.stream_sharing_ = StreamSharing::None;
      break;
    }
    rule.skip_if_empty_ = proto_rule.skip_if_empty();
    rule.value_ = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
        proto_rule.format_string(), context);
    rules.push_back(rule);
  }
  return rules;
}

void Config::updateFilterState(const Formatter::HttpFormatterContext& context,
                               StreamInfo::StreamInfo& info) const {
  for (const auto& rule : rules_) {
    const std::string value = rule.value_->formatWithContext(context, info);
    if (value.empty() && rule.skip_if_empty_) {
      ENVOY_LOG(trace, "Skip empty value for an object '{}'", rule.key_);
      continue;
    }
    auto object = rule.factory_->createFromBytes(value);
    if (object == nullptr) {
      ENVOY_LOG(trace, "Failed to create an object '{}' from value '{}'", rule.key_, value);
      continue;
    }
    ENVOY_LOG(trace, "Set the filter state to '{}'", object->serializeAsString().value_or(""));
    info.filterState()->setData(rule.key_, std::move(object), rule.state_type_, life_span_,
                                rule.stream_sharing_);
  }
}

} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
