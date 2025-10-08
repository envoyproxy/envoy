#include "source/common/upstream/transport_socket_input.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.validate.h"
#include "envoy/type/metadata/v3/metadata.pb.h"
#include "envoy/type/metadata/v3/metadata.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/json/json_utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

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
  using ProtoEndpointMetadataInput =
      envoy::extensions::matching::common_inputs::transport_socket::v3::EndpointMetadataInput;
  const auto& endpoint_metadata_input_proto =
      dynamic_cast<const ProtoEndpointMetadataInput&>(config);

  // Extract filter and path from the endpoint metadata input.
  std::string filter = endpoint_metadata_input_proto.filter().empty()
                           ? std::string("envoy.transport_socket_match")
                           : std::string(endpoint_metadata_input_proto.filter());
  std::vector<std::string> path;
  if (endpoint_metadata_input_proto.path_size() > 0) {
    path.reserve(endpoint_metadata_input_proto.path_size());
    for (const auto& segment : endpoint_metadata_input_proto.path()) {
      // Only key segments are supported per proto.
      if (segment.has_key()) {
        path.push_back(segment.key());
      }
    }
  }

  return [filter = std::move(filter), path = std::move(path)]() {
    return std::make_unique<Upstream::EndpointMetadataInput>(filter, path);
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
  using ProtoLocalityMetadataInput =
      envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput;
  const auto& locality_metadata_input_proto =
      dynamic_cast<const ProtoLocalityMetadataInput&>(config);

  // Extract filter and path from the locality metadata input.
  std::string filter = locality_metadata_input_proto.filter().empty()
                           ? std::string("envoy.transport_socket_match")
                           : std::string(locality_metadata_input_proto.filter());
  std::vector<std::string> path;
  if (locality_metadata_input_proto.path_size() > 0) {
    path.reserve(locality_metadata_input_proto.path_size());
    for (const auto& segment : locality_metadata_input_proto.path()) {
      // Only key segments are supported per proto.
      if (segment.has_key()) {
        path.push_back(segment.key());
      }
    }
  }

  return [filter = std::move(filter), path = std::move(path)]() {
    return std::make_unique<Upstream::LocalityMetadataInput>(filter, path);
  };
}

ProtobufTypes::MessagePtr LocalityMetadataInputFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::matching::common_inputs::transport_socket::v3::LocalityMetadataInput>();
}

// Register factories for transport socket specific inputs.
// Note: Network inputs have transport-socket-specific names (e.g.,
// envoy.matching.inputs.transport_socket.destination_ip) to distinguish them from the
// generic network inputs that work with Network::Matching::MatchingData.
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

} // namespace Upstream
} // namespace Envoy
