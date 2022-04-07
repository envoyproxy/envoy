#pragma once

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"

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
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using DestinationIPInputFactory = DestinationIPInputBaseFactory<MatchingData>;
using UdpDestinationIPInputFactory = DestinationIPInputBaseFactory<UdpMatchingData>;

template <class MatchingDataType>
class DestinationPortInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using DestinationPortInputFactory = DestinationPortInputBaseFactory<MatchingData>;
using UdpDestinationPortInputFactory = DestinationPortInputBaseFactory<UdpMatchingData>;

template <class MatchingDataType>
class SourceIPInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using SourceIPInputFactory = SourceIPInputBaseFactory<MatchingData>;
using UdpSourceIPInputFactory = SourceIPInputBaseFactory<UdpMatchingData>;

template <class MatchingDataType>
class SourcePortInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using SourcePortInputFactory = SourcePortInputBaseFactory<MatchingData>;
using UdpSourcePortInputFactory = SourcePortInputBaseFactory<UdpMatchingData>;

template <class MatchingDataType>
class DirectSourceIPInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using DirectSourceIPInputFactory = DirectSourceIPInputBaseFactory<MatchingData>;

template <class MatchingDataType>
class SourceTypeInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
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

using SourceTypeInputFactory = SourceTypeInputBaseFactory<MatchingData>;

template <class MatchingDataType>
class ServerNameInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
};

template <class MatchingDataType>
class ServerNameBaseFactory
    : public BaseFactory<ServerNameInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::network::v3::ServerNameInput,
                         MatchingDataType> {
public:
  ServerNameBaseFactory()
      : BaseFactory<ServerNameInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::ServerNameInput,
                    MatchingDataType>("server_name") {}
};

using ServerNameInputFactory = ServerNameBaseFactory<MatchingData>;

template <class MatchingDataType>
class TransportProtocolInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
};

template <class MatchingDataType>
class TransportProtocolBaseFactory
    : public BaseFactory<
          TransportProtocolInput<MatchingDataType>,
          envoy::extensions::matching::common_inputs::network::v3::TransportProtocolInput,
          MatchingDataType> {
public:
  TransportProtocolBaseFactory()
      : BaseFactory<TransportProtocolInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::network::v3::TransportProtocolInput,
                    MatchingDataType>("transport_protocol") {}
};

using TransportProtocolInputFactory = TransportProtocolBaseFactory<MatchingData>;

template <class MatchingDataType>
class ApplicationProtocolInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override;
};

template <class MatchingDataType>
class ApplicationProtocolBaseFactory
    : public BaseFactory<
          ApplicationProtocolInput<MatchingDataType>,
          envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput,
          MatchingDataType> {
public:
  ApplicationProtocolBaseFactory()
      : BaseFactory<
            ApplicationProtocolInput<MatchingDataType>,
            envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput,
            MatchingDataType>("application_protocol") {}
};

using ApplicationProtocolInputFactory = ApplicationProtocolBaseFactory<MatchingData>;

} // namespace Matching
} // namespace Network
} // namespace Envoy
