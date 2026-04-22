#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/transport_socket/v3/transport_socket_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/transport_socket_matching_data.h"

#include "source/common/config/metadata.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace TransportSocket {

/**
 * Base class for transport socket data input implementations.
 */
class TransportSocketInputBase : public Matcher::DataInput<Upstream::TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const Upstream::TransportSocketMatchingData& data) const override;

protected:
  /**
   * Extract the specific value from the matching data.
   * @param data the transport socket matching data.
   * @return the extracted string value or absl::nullopt if not available.
   */
  virtual absl::optional<std::string>
  getValue(const Upstream::TransportSocketMatchingData& data) const PURE;
};

/**
 * Data input for extracting endpoint metadata values.
 */
class EndpointMetadataInput : public TransportSocketInputBase {
public:
  EndpointMetadataInput(const std::string& filter, const std::vector<std::string>& path)
      : filter_(filter), path_(path) {}

protected:
  absl::optional<std::string>
  getValue(const Upstream::TransportSocketMatchingData& data) const override;

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
  absl::optional<std::string>
  getValue(const Upstream::TransportSocketMatchingData& data) const override;

private:
  const std::string filter_;
  const std::vector<std::string> path_;
};

/**
 * Factory for creating endpoint metadata data inputs.
 */
class EndpointMetadataInputFactory
    : public Matcher::DataInputFactory<Upstream::TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.endpoint_metadata"; }

  Matcher::DataInputFactoryCb<Upstream::TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

/**
 * Factory for creating locality metadata data inputs.
 */
class LocalityMetadataInputFactory
    : public Matcher::DataInputFactory<Upstream::TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.locality_metadata"; }

  Matcher::DataInputFactoryCb<Upstream::TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
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
  absl::optional<std::string>
  getValue(const Upstream::TransportSocketMatchingData& data) const override;

private:
  const std::string key_;
};

/**
 * Factory for creating filter state data inputs.
 */
class FilterStateInputFactory
    : public Matcher::DataInputFactory<Upstream::TransportSocketMatchingData> {
public:
  std::string name() const override {
    return "envoy.matching.inputs.transport_socket_filter_state";
  }

  Matcher::DataInputFactoryCb<Upstream::TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
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
} // namespace Envoy
