#include "source/extensions/filters/common/set_filter_state/filter_config.h"

#include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {

std::vector<Value>
Config::parse(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values,
              Server::Configuration::GenericFactoryContext& context) const {
  std::vector<Value> values;
  values.reserve(proto_values.size());
  for (const auto& proto_value : proto_values) {
    Value value;
    value.key_ = proto_value.object_key();
    value.factory_ =
        Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(value.key_);
    if (value.factory_ == nullptr) {
      throw EnvoyException(fmt::format("'{}' does not have an object factory", value.key_));
    }
    value.state_type_ = proto_value.read_only() ? StateType::ReadOnly : StateType::Mutable;
    switch (proto_value.shared_with_upstream()) {
    case FilterStateValueProto::ONCE:
      value.stream_sharing_ = StreamSharing::SharedWithUpstreamConnectionOnce;
      break;
    case FilterStateValueProto::TRANSITIVE:
      value.stream_sharing_ = StreamSharing::SharedWithUpstreamConnection;
      break;
    default:
      value.stream_sharing_ = StreamSharing::None;
      break;
    }
    value.skip_if_empty_ = proto_value.skip_if_empty();
    value.value_ = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
        proto_value.format_string(), context);
    values.push_back(std::move(value));
  }
  return values;
}

void Config::updateFilterState(const Formatter::HttpFormatterContext& context,
                               StreamInfo::StreamInfo& info) const {
  for (const auto& value : values_) {
    const std::string bytes_value = value.value_->formatWithContext(context, info);
    if (bytes_value.empty() && value.skip_if_empty_) {
      ENVOY_LOG(debug, "Skip empty value for an object '{}'", value.key_);
      continue;
    }
    auto object = value.factory_->createFromBytes(bytes_value);
    if (object == nullptr) {
      ENVOY_LOG(debug, "Failed to create an object '{}' from value '{}'", value.key_, bytes_value);
      continue;
    }
    ENVOY_LOG(debug, "Created the filter state '{}' from value '{}'", value.key_, bytes_value);
    info.filterState()->setData(value.key_, std::move(object), value.state_type_, life_span_,
                                value.stream_sharing_);
  }
}

} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
