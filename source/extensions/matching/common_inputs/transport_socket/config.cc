#include "source/extensions/matching/common_inputs/transport_socket/config.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.validate.h"
#include "envoy/registry/registry.h"

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

namespace {

/**
 * Convert a protobuf Value to string using comprehensive type handling.
 * @param value The protobuf Value to convert.
 * @return String representation of the value.
 */
std::string valueToString(const Protobuf::Value& value) {
  std::string result;
  switch (value.kind_case()) {
  case Protobuf::Value::kStringValue:
    return value.string_value();
  case Protobuf::Value::kNumberValue: {
    // Convert numbers to strings without unnecessary decimal places.
    double num_value = value.number_value();
    if (num_value == std::floor(num_value)) {
      // It's an integer, format without decimal places.
      return std::to_string(static_cast<int64_t>(num_value));
    } else {
      // It's a float, use default formatting.
      return std::to_string(num_value);
    }
  }
  case Protobuf::Value::kBoolValue:
    return value.bool_value() ? "true" : "false";
  default:
    // For other types (struct, list, null), use JSON serialization.
    Json::Utility::appendValueToString(value, result);
    return result;
  }
}

/**
 * Anonymous helper function to extract metadata values using filter and path.
 * Shared between endpoint and locality metadata inputs to avoid code duplication.
 * @param metadata The metadata source to extract from.
 * @param filter The filter name for metadata extraction.
 * @param path The path segments for nested metadata extraction.
 * @return Optional string value extracted from metadata, nullopt if not found.
 */
absl::optional<std::string> extractMetadataValue(const envoy::config::core::v3::Metadata* metadata,
                                                 const std::string& filter,
                                                 const std::vector<std::string>& path) {
  if (!metadata) {
    return absl::nullopt;
  }

  // Use comprehensive metadata extraction with filter and path support.
  const Protobuf::Value& value = Config::Metadata::metadataValue(metadata, filter, path);

  // Convert the protobuf value to string using comprehensive type handling.
  std::string result = valueToString(value);

  if (result.empty()) {
    return absl::nullopt;
  }

  return result;
}

} // namespace

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
  return extractMetadataValue(data.endpoint_metadata_, filter_, path_);
}

absl::optional<std::string>
LocalityMetadataInput::getValue(const TransportSocketMatchingData& data) const {
  return extractMetadataValue(data.locality_metadata_, filter_, path_);
}

Matcher::DataInputFactoryCb<TransportSocketMatchingData>
EndpointMetadataInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  UNREFERENCED_PARAMETER(validation_visitor);
  const auto& metadata_input_proto =
      dynamic_cast<const envoy::extensions::matching::common_inputs::transport_socket::v3::
                       EndpointMetadataInput&>(config);

  // Extract filter and path from the metadata input.
  std::string filter = metadata_input_proto.filter();
  std::vector<std::string> path;
  if (metadata_input_proto.path_size() > 0) {
    path.reserve(metadata_input_proto.path_size());
    for (const auto& segment : metadata_input_proto.path()) {
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
  const auto& metadata_input_proto =
      dynamic_cast<const envoy::extensions::matching::common_inputs::transport_socket::v3::
                       LocalityMetadataInput&>(config);

  // Extract filter and path from the metadata input.
  std::string filter = metadata_input_proto.filter();
  std::vector<std::string> path;
  if (metadata_input_proto.path_size() > 0) {
    path.reserve(metadata_input_proto.path_size());
    for (const auto& segment : metadata_input_proto.path()) {
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

Matcher::DataInputGetResult DestinationIPInput::get(const TransportSocketMatchingData& data) const {
  if (data.local_address_ == nullptr) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  const auto& address = *data.local_address_;
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address.ip()->addressAsString()};
}

Matcher::DataInputGetResult SourceIPInput::get(const TransportSocketMatchingData& data) const {
  if (data.remote_address_ == nullptr) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  const auto& address = *data.remote_address_;
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address.ip()->addressAsString()};
}

Matcher::DataInputGetResult
DestinationPortInput::get(const TransportSocketMatchingData& data) const {
  if (data.local_address_ == nullptr) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  const auto& address = *data.local_address_;
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address.ip()->port())};
}

Matcher::DataInputGetResult SourcePortInput::get(const TransportSocketMatchingData& data) const {
  if (data.remote_address_ == nullptr) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  const auto& address = *data.remote_address_;
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address.ip()->port())};
}

Matcher::DataInputGetResult ServerNameInput::get(const TransportSocketMatchingData& data) const {
  // First try the explicit server name if provided.
  if (!data.server_name_.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.server_name_)};
  }

  // Fall back to connection info provider if available.
  if (data.connection_info_) {
    const auto server_name = data.connection_info_->requestedServerName();
    if (!server_name.empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(server_name)};
    }
  }

  return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
}

Matcher::DataInputGetResult
ApplicationProtocolInput::get(const TransportSocketMatchingData& data) const {
  if (data.application_protocols_ != nullptr && !data.application_protocols_->empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            absl::StrJoin(*data.application_protocols_, ",")};
  }
  return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
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

// Register factories for transport socket matching inputs.
// The endpoint_metadata and locality_metadata inputs are unique to transport socket matching.
// The network attribute inputs (destination_ip, source_ip, etc.) are also registered here
// as separate implementations for the TransportSocketMatchingData type, even though factories
// with the same names exist for Network::Matching::MatchingData. The factory registry is
// type-safe and maintains separate registries for each matching data type, so there's no conflict.
// These implementations handle the nullable pointers and context-specific data access patterns
// required for transport socket matching.
REGISTER_FACTORY(EndpointMetadataInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(LocalityMetadataInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(DestinationIPInputFactory, Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(SourceIPInputFactory, Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(DestinationPortInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(SourcePortInputFactory, Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(ServerNameInputFactory, Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(ApplicationProtocolInputFactory,
                 Matcher::DataInputFactory<TransportSocketMatchingData>);
REGISTER_FACTORY(TransportSocketNameActionFactory,
                 Matcher::ActionFactory<Server::Configuration::ServerFactoryContext>);

} // namespace TransportSocket
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
