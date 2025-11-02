#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/config/metadata.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace TransportSocket {

/**
 * Data structure holding context for transport socket matching.
 * This provides access to:
 * - Endpoint metadata: metadata associated with the selected upstream endpoint
 * - Locality metadata: metadata associated with the endpoint's locality
 * - Filter state: shared filter state from downstream connection (via TransportSocketOptions)
 *
 * Filter state enables downstream-connection-based matching by allowing filters to explicitly
 * pass any data (e.g., network namespace, custom attributes) from downstream to upstream.
 * This follows the same pattern as tunneling in Envoy.
 */
struct TransportSocketMatchingData {
  static absl::string_view name() { return "transport_socket"; }

  TransportSocketMatchingData(const envoy::config::core::v3::Metadata* endpoint_metadata,
                              const envoy::config::core::v3::Metadata* locality_metadata,
                              const StreamInfo::FilterState* filter_state = nullptr)
      : endpoint_metadata_(endpoint_metadata), locality_metadata_(locality_metadata),
        filter_state_(filter_state) {}

  const envoy::config::core::v3::Metadata* endpoint_metadata_;
  const envoy::config::core::v3::Metadata* locality_metadata_;
  const StreamInfo::FilterState* filter_state_;
};

/**
 * Base class for transport socket data input implementations.
 */
class TransportSocketInputBase : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override;

protected:
  /**
   * Extract the specific value from the matching data.
   * @param data the transport socket matching data.
   * @return the extracted string value or absl::nullopt if not available.
   */
  virtual absl::optional<std::string> getValue(const TransportSocketMatchingData& data) const PURE;
};

/**
 * Data input for extracting endpoint metadata values.
 */
class EndpointMetadataInput : public TransportSocketInputBase {
public:
  EndpointMetadataInput(const std::string& filter, const std::vector<std::string>& path)
      : filter_(filter), path_(path) {}

protected:
  absl::optional<std::string> getValue(const TransportSocketMatchingData& data) const override;

private:
  const std::string filter_;
  const std::vector<std::string> path_;
};

/**
 * Data input for extracting locality metadata values.
 */
class LocalityMetadataInput : public TransportSocketInputBase {
public:
  LocalityMetadataInput(const std::string& filter, const std::vector<std::string>& path)
      : filter_(filter), path_(path) {}

protected:
  absl::optional<std::string> getValue(const TransportSocketMatchingData& data) const override;

private:
  const std::string filter_;
  const std::vector<std::string> path_;
};

/**
 * Factory for creating endpoint metadata data inputs.
 */
class EndpointMetadataInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.endpoint_metadata"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Factory for creating locality metadata data inputs.
 */
class LocalityMetadataInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.locality_metadata"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for extracting values from filter state.
 * This enables downstream-connection-based matching by reading filter state that was
 * explicitly shared from downstream to upstream via TransportSocketOptions.
 */
class FilterStateInput : public TransportSocketInputBase {
public:
  explicit FilterStateInput(const std::string& key) : key_(key) {}

protected:
  absl::optional<std::string> getValue(const TransportSocketMatchingData& data) const override;

private:
  const std::string key_;
};

/**
 * Factory for creating filter state data inputs.
 */
class FilterStateInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.filter_state"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Action that carries a transport socket name.
 */
class TransportSocketNameAction : public Matcher::Action {
public:
  explicit TransportSocketNameAction(const std::string& name) : name_(name) {}
  const std::string& name() const { return name_; }
  absl::string_view typeUrl() const override {
    return "type.googleapis.com/"
           "envoy.extensions.matching.common_inputs.transport_socket.v3.TransportSocketNameAction";
  }

private:
  const std::string name_;
};

/**
 * ActionFactory that creates TransportSocketNameAction.
 */
class TransportSocketNameActionFactory
    : public Matcher::ActionFactory<Server::Configuration::ServerFactoryContext> {
public:
  std::string name() const override { return "envoy.matching.action.transport_socket.name"; }
  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message& config,
                                             Server::Configuration::ServerFactoryContext&,
                                             ProtobufMessage::ValidationVisitor&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace TransportSocket
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions

// Export TransportSocketMatchingData to the Upstream namespace for backward compatibility.
namespace Upstream {
using TransportSocketMatchingData =
    Extensions::Matching::CommonInputs::TransportSocket::TransportSocketMatchingData;
} // namespace Upstream

} // namespace Envoy
