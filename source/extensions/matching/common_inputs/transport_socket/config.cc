#include "source/extensions/matching/common_inputs/transport_socket/config.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/json/json_utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace TransportSocket {

Matcher::DataInputGetResult
TransportSocketInputBase::get(const TransportSocketMatchingData& data) const {
  auto value = getValue(data);
  if (value.has_value()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, value.value()};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
}

absl::optional<std::string>
EndpointMetadataInput::getValue(const TransportSocketMatchingData& data) const {
  if (!data.endpoint_metadata_) {
    return absl::nullopt;
  }

  // Use metadata extraction with filter and path support.
  const Protobuf::Value& value =
      Config::Metadata::metadataValue(data.endpoint_metadata_, filter_, path_);

  // Convert the protobuf value to string.
  std::string result;
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    result = value.string_value();
  } else {
    Json::Utility::appendValueToString(value, result);
  }

  if (result.empty()) {
    return absl::nullopt;
  }

  return result;
}

absl::optional<std::string>
LocalityMetadataInput::getValue(const TransportSocketMatchingData& data) const {
  if (!data.locality_metadata_) {
    return absl::nullopt;
  }

  // Use metadata extraction with filter and path support.
  const Protobuf::Value& value =
      Config::Metadata::metadataValue(data.locality_metadata_, filter_, path_);

  // Convert the protobuf value to string.
  std::string result;
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    result = value.string_value();
  } else {
    Json::Utility::appendValueToString(value, result);
  }

  if (result.empty()) {
    return absl::nullopt;
  }

  return result;
}

Matcher::DataInputFactoryCb<TransportSocketMatchingData>
EndpointMetadataInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  UNREFERENCED_PARAMETER(validation_visitor);
  const auto& typed_config = dynamic_cast<const envoy::extensions::matching::common_inputs::
                                              transport_socket::v3::EndpointMetadataInput&>(config);

  std::string filter = typed_config.filter().empty()
                           ? std::string(Envoy::Config::MetadataFilters::get().ENVOY_LB)
                           : std::string(typed_config.filter());

  std::vector<std::string> path;
  if (typed_config.path_size() > 0) {
    path.reserve(typed_config.path_size());
    for (const auto& segment : typed_config.path()) {
      // Only key segments are supported per proto.
      if (segment.has_key()) {
        path.push_back(segment.key());
      }
    }
  }

  return [filter = std::move(filter), path = std::move(path)]() {
    return std::make_unique<EndpointMetadataInput>(filter, path);
  };
}

ProtobufTypes::MessagePtr EndpointMetadataInputFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput>();
}

Matcher::DataInputFactoryCb<TransportSocketMatchingData>
LocalityMetadataInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  UNREFERENCED_PARAMETER(validation_visitor);
  const auto& typed_config = dynamic_cast<const envoy::extensions::matching::common_inputs::
                                              transport_socket::v3::LocalityMetadataInput&>(config);

  std::string filter = typed_config.filter().empty()
                           ? std::string(Envoy::Config::MetadataFilters::get().ENVOY_LB)
                           : std::string(typed_config.filter());

  std::vector<std::string> path;
  if (typed_config.path_size() > 0) {
    path.reserve(typed_config.path_size());
    for (const auto& segment : typed_config.path()) {
      // Only key segments are supported per proto.
      if (segment.has_key()) {
        path.push_back(segment.key());
      }
    }
  }

  return [filter = std::move(filter), path = std::move(path)]() {
    return std::make_unique<LocalityMetadataInput>(filter, path);
  };
}

ProtobufTypes::MessagePtr LocalityMetadataInputFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput>();
}

absl::optional<std::string>
FilterStateInput::getValue(const TransportSocketMatchingData& data) const {
  if (!data.filter_state_) {
    return absl::nullopt;
  }

  // Try to get the filter state object by key.
  const auto* object = data.filter_state_->getDataReadOnly<StreamInfo::FilterState::Object>(key_);
  if (!object) {
    return absl::nullopt;
  }

  // Try to serialize the object to a string.
  const auto serialized = object->serializeAsString();
  if (!serialized.has_value() || serialized->empty()) {
    return absl::nullopt;
  }

  return serialized.value();
}

Matcher::DataInputFactoryCb<TransportSocketMatchingData>
FilterStateInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  UNREFERENCED_PARAMETER(validation_visitor);
  const auto& typed_config = dynamic_cast<
      const envoy::extensions::matching::common_inputs::transport_socket::v3::FilterStateInput&>(
      config);

  std::string key = typed_config.key();
  return [key = std::move(key)]() { return std::make_unique<FilterStateInput>(key); };
}

ProtobufTypes::MessagePtr FilterStateInputFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::matching::common_inputs::transport_socket::v3::FilterStateInput>();
}

Matcher::ActionConstSharedPtr
TransportSocketNameActionFactory::createAction(const Protobuf::Message& config,
                                               Server::Configuration::ServerFactoryContext&,
                                               ProtobufMessage::ValidationVisitor&) {
  const auto& typed_config =
      dynamic_cast<const envoy::extensions::matching::common_inputs::transport_socket::v3::
                       TransportSocketNameAction&>(config);
  return std::make_shared<TransportSocketNameAction>(typed_config.name());
}

ProtobufTypes::MessagePtr TransportSocketNameActionFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::matching::common_inputs::transport_socket::v3::
                              TransportSocketNameAction>();
}

// Register factories for transport socket matchers.
REGISTER_FACTORY(EndpointMetadataInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(LocalityMetadataInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(FilterStateInputFactory, Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(TransportSocketNameActionFactory,
                 Matcher::ActionFactory<Server::Configuration::ServerFactoryContext>);

} // namespace TransportSocket
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
