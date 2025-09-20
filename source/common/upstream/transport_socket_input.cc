#include "source/common/upstream/transport_socket_input.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/type/metadata/v3/metadata.pb.h"
#include "envoy/type/metadata/v3/metadata.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/json/json_utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

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
  // Expect a MetadataKey describing which metadata to read.
  const auto& metadata_key = dynamic_cast<const envoy::type::metadata::v3::MetadataKey&>(config);

  std::string filter =
      metadata_key.key().empty() ? std::string("envoy.lb") : std::string(metadata_key.key());
  std::vector<std::string> path;
  if (metadata_key.path_size() > 0) {
    path.reserve(metadata_key.path_size());
    for (const auto& segment : metadata_key.path()) {
      // Only key segments are supported per proto.
      if (segment.has_key()) {
        path.push_back(segment.key());
      }
    }
  } else {
    // Default to reading key "type" within the filter if no path provided.
    path.push_back("type");
  }

  return [filter = std::move(filter), path = std::move(path)]() {
    return std::make_unique<EndpointMetadataInput>(filter, path);
  };
}

ProtobufTypes::MessagePtr EndpointMetadataInputFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::type::metadata::v3::MetadataKey>();
}

Matcher::DataInputFactoryCb<TransportSocketMatchingData>
LocalityMetadataInputFactory::createDataInputFactoryCb(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  UNREFERENCED_PARAMETER(config);
  UNREFERENCED_PARAMETER(validation_visitor);
  // Use default transport socket matching filter and empty path for simplicity.
  std::string filter = Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKET_MATCH;
  std::vector<std::string> path;

  return [filter, path]() { return std::make_unique<LocalityMetadataInput>(filter, path); };
}

ProtobufTypes::MessagePtr LocalityMetadataInputFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::core::v3::SocketAddress>();
}

// Register factories for all transport socket specific inputs.
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
