#pragma once

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <class InputType, class ProtoType, class MatchingDataType>
class BaseFactory : public Matcher::DataInputFactory<MatchingDataType> {
protected:
  explicit BaseFactory(const std::string& name) : name_(name) {}

public:
  std::string name() const override { return "envoy.matching.inputs." + name_; }

  Matcher::DataInputFactoryCb<MatchingDataType>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<InputType>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

template <class MatchingDataType>
class DestinationIPInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& address = data.localAddress();

    if (address.type() != Network::Address::Type::Ip) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            address.ip()->addressAsString()};
  }
};

template <class MatchingDataType>
class DestinationIPInputBaseFactory
    : public BaseFactory<
          DestinationIPInput<MatchingDataType>,
          envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput,
          MatchingDataType> {
public:
  DestinationIPInputBaseFactory()
      : BaseFactory<DestinationIPInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput,
                    MatchingDataType>("destination_ip") {}
};

DECLARE_FACTORY(DestinationIPInputFactory);
DECLARE_FACTORY(UdpDestinationIPInputFactory);
DECLARE_FACTORY(HttpDestinationIPInputFactory);

// With the support of generic matching API, integer is allowed in inputs such as
// `DestinationPortInput` and `SourcePortInput`. As a result, there is no longer a need for the
// redundant conversion of int-to-string. We can change them here once IntInputMatcher (for integer
// type input) is implemented.
template <class MatchingDataType>
class DestinationPortInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& address = data.localAddress();
    if (address.type() != Network::Address::Type::Ip) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            absl::StrCat(address.ip()->port())};
  }
};

template <class MatchingDataType>
class DestinationPortInputBaseFactory
    : public BaseFactory<
          DestinationPortInput<MatchingDataType>,
          envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput,
          MatchingDataType> {
public:
  DestinationPortInputBaseFactory()
      : BaseFactory<DestinationPortInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput,
                    MatchingDataType>("destination_port") {}
};

DECLARE_FACTORY(DestinationPortInputFactory);
DECLARE_FACTORY(UdpDestinationPortInputFactory);
DECLARE_FACTORY(HttpDestinationPortInputFactory);

template <class MatchingDataType>
class SourceIPInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& address = data.remoteAddress();
    if (address.type() != Network::Address::Type::Ip) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            address.ip()->addressAsString()};
  }
};

template <class MatchingDataType>
class SourceIPInputBaseFactory
    : public BaseFactory<SourceIPInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::network::v3::SourceIPInput,
                         MatchingDataType> {
public:
  SourceIPInputBaseFactory()
      : BaseFactory<SourceIPInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::SourceIPInput,
                    MatchingDataType>("source_ip") {}
};

DECLARE_FACTORY(SourceIPInputFactory);
DECLARE_FACTORY(UdpSourceIPInputFactory);
DECLARE_FACTORY(HttpSourceIPInputFactory);

template <class MatchingDataType>
class SourcePortInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& address = data.remoteAddress();
    if (address.type() != Network::Address::Type::Ip) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            absl::StrCat(address.ip()->port())};
  }
};

template <class MatchingDataType>
class SourcePortInputBaseFactory
    : public BaseFactory<SourcePortInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::network::v3::SourcePortInput,
                         MatchingDataType> {
public:
  SourcePortInputBaseFactory()
      : BaseFactory<SourcePortInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::SourcePortInput,
                    MatchingDataType>("source_port") {}
};

DECLARE_FACTORY(SourcePortInputFactory);
DECLARE_FACTORY(UdpSourcePortInputFactory);
DECLARE_FACTORY(HttpSourcePortInputFactory);

template <class MatchingDataType>
class DirectSourceIPInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& address = data.connectionInfoProvider().directRemoteAddress();
    if (address->type() != Network::Address::Type::Ip) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            address->ip()->addressAsString()};
  }
};

template <class MatchingDataType>
class DirectSourceIPInputBaseFactory
    : public BaseFactory<
          DirectSourceIPInput<MatchingDataType>,
          envoy::extensions::matching::common_inputs::network::v3::DirectSourceIPInput,
          MatchingDataType> {
public:
  DirectSourceIPInputBaseFactory()
      : BaseFactory<DirectSourceIPInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::DirectSourceIPInput,
                    MatchingDataType>("direct_source_ip") {}
};

DECLARE_FACTORY(DirectSourceIPInputFactory);
DECLARE_FACTORY(HttpDirectSourceIPInputFactory);

template <class MatchingDataType>
class SourceTypeInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const bool is_local_connection =
        Network::Utility::isSameIpOrLoopback(data.connectionInfoProvider());
    if (is_local_connection) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, "local"};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }
};

template <class MatchingDataType>
class SourceTypeInputBaseFactory
    : public BaseFactory<SourceTypeInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::network::v3::SourceTypeInput,
                         MatchingDataType> {
public:
  SourceTypeInputBaseFactory()
      : BaseFactory<SourceTypeInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::SourceTypeInput,
                    MatchingDataType>("source_type") {}
};

DECLARE_FACTORY(SourceTypeInputFactory);
DECLARE_FACTORY(HttpSourceTypeInputFactory);

template <class MatchingDataType>
class ServerNameInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto server_name = data.connectionInfoProvider().requestedServerName();
    if (!server_name.empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(server_name)};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }
};

template <class MatchingDataType>
class ServerNameInputBaseFactory
    : public BaseFactory<ServerNameInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::network::v3::ServerNameInput,
                         MatchingDataType> {
public:
  ServerNameInputBaseFactory()
      : BaseFactory<ServerNameInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::ServerNameInput,
                    MatchingDataType>("server_name") {}
};

DECLARE_FACTORY(ServerNameInputFactory);
DECLARE_FACTORY(HttpServerNameInputFactory);

class TransportProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class TransportProtocolInputFactory
    : public BaseFactory<
          TransportProtocolInput,
          envoy::extensions::matching::common_inputs::network::v3::TransportProtocolInput,
          MatchingData> {
public:
  TransportProtocolInputFactory() : BaseFactory("transport_protocol") {}
};

DECLARE_FACTORY(TransportProtocolInputFactory);

class FilterStateInput : public Matcher::DataInput<MatchingData> {
public:
  FilterStateInput(
      const envoy::extensions::matching::common_inputs::network::v3::FilterStateInput& input_config)
      : filter_state_key_(input_config.key()) {}

  Matcher::DataInputGetResult get(const MatchingData& data) const override;

private:
  const std::string filter_state_key_;
};

class FilterStateInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.filter_state"; }

  Matcher::DataInputFactoryCb<MatchingData> createDataInputFactoryCb(
      const Protobuf::Message& message,
      ProtobufMessage::ValidationVisitor& message_validation_visitor) override {
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::common_inputs::network::v3::FilterStateInput&>(
        message, message_validation_visitor);

    return [proto_config]() { return std::make_unique<FilterStateInput>(proto_config); };
  };

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::network::v3::FilterStateInput>();
  }
};

DECLARE_FACTORY(FilterStateInputFactory);

} // namespace Matching
} // namespace Network
} // namespace Envoy
