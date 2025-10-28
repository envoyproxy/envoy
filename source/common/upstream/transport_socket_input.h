#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

/**
 * Data structure holding context for transport socket matching.
 * This provides access to endpoint metadata, locality metadata, and network connection context
 * that can be used by the matcher framework to select transport sockets.
 */
struct TransportSocketMatchingData {
  static absl::string_view name() { return "transport_socket"; }

  TransportSocketMatchingData(const envoy::config::core::v3::Metadata* endpoint_metadata,
                              const envoy::config::core::v3::Metadata* locality_metadata,
                              const Network::Address::Instance* local_address = nullptr,
                              const Network::Address::Instance* remote_address = nullptr,
                              const Network::ConnectionInfoProvider* connection_info = nullptr,
                              const StreamInfo::FilterState* filter_state = nullptr,
                              absl::string_view server_name = "",
                              const std::vector<std::string>* application_protocols = nullptr)
      : endpoint_metadata_(endpoint_metadata), locality_metadata_(locality_metadata),
        local_address_(local_address), remote_address_(remote_address),
        connection_info_(connection_info), filter_state_(filter_state), server_name_(server_name),
        application_protocols_(application_protocols) {}

  const envoy::config::core::v3::Metadata* endpoint_metadata_;
  const envoy::config::core::v3::Metadata* locality_metadata_;
  const Network::Address::Instance* local_address_;
  const Network::Address::Instance* remote_address_;
  const Network::ConnectionInfoProvider* connection_info_;
  const StreamInfo::FilterState* filter_state_;
  absl::string_view server_name_;
  const std::vector<std::string>* application_protocols_;
};

/**
 * Base class for transport socket data input factories.
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

// Transport socket specific network inputs.
// These are separate from the standard network inputs because they operate on
// TransportSocketMatchingData instead of Network::Matching::MatchingData.

/**
 * Data input for destination IP address from transport socket matching context.
 */
class DestinationIPInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
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
};

/**
 * Factory for creating destination IP data inputs for transport socket matching.
 */
class DestinationIPInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.destination_ip"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<DestinationIPInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for source IP address from transport socket matching context.
 */
class SourceIPInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
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
};

/**
 * Factory for creating source IP data inputs for transport socket matching.
 */
class SourceIPInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.source_ip"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SourceIPInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::SourceIPInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for destination port from transport socket matching context.
 */
class DestinationPortInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
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
};

/**
 * Factory for creating destination port data inputs for transport socket matching.
 */
class DestinationPortInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.destination_port"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<DestinationPortInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for source port from transport socket matching context.
 */
class SourcePortInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
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
};

/**
 * Factory for creating source port data inputs for transport socket matching.
 */
class SourcePortInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.source_port"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SourcePortInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::SourcePortInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for server name indication (SNI) from transport socket matching context.
 */
class ServerNameInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
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
};

/**
 * Factory for creating server name data inputs for transport socket matching.
 */
class ServerNameInputFactory : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.server_name"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ServerNameInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::ServerNameInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

/**
 * Data input for application protocols (ALPN).
 * This is transport-socket-specific since it reads from the TransportSocketMatchingData.
 */
class ApplicationProtocolInput : public Matcher::DataInput<TransportSocketMatchingData> {
public:
  Matcher::DataInputGetResult get(const TransportSocketMatchingData& data) const override {
    if (data.application_protocols_ != nullptr && !data.application_protocols_->empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::StrJoin(*data.application_protocols_, ",")};
    }
    return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
  }
};

/**
 * Factory for creating application protocol data inputs.
 */
class ApplicationProtocolInputFactory
    : public Matcher::DataInputFactory<TransportSocketMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.application_protocols"; }

  Matcher::DataInputFactoryCb<TransportSocketMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ApplicationProtocolInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput>();
  }

  std::string category() const override { return "envoy.matching.inputs"; }
};

} // namespace Upstream
} // namespace Envoy
