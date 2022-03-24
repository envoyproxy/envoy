#pragma once

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <class InputType, class ProtoType>
class BaseFactory : public Matcher::DataInputFactory<MatchingData> {
protected:
  explicit BaseFactory(const std::string& name) : name_(name) {}

public:
  std::string name() const override { return "envoy.matching.inputs." + name_; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<InputType>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

class DestinationIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DestinationIPInputFactory
    : public BaseFactory<
          DestinationIPInput,
          envoy::extensions::matching::common_inputs::network::v3::DestinationIPInput> {
public:
  DestinationIPInputFactory() : BaseFactory("destination_ip") {}
};

class DestinationPortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DestinationPortInputFactory
    : public BaseFactory<
          DestinationPortInput,
          envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput> {
public:
  DestinationPortInputFactory() : BaseFactory("destination_port") {}
};

class SourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceIPInputFactory
    : public BaseFactory<SourceIPInput,
                         envoy::extensions::matching::common_inputs::network::v3::SourceIPInput> {
public:
  SourceIPInputFactory() : BaseFactory("source_ip") {}
};

class SourcePortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourcePortInputFactory
    : public BaseFactory<SourcePortInput,
                         envoy::extensions::matching::common_inputs::network::v3::SourcePortInput> {
public:
  SourcePortInputFactory() : BaseFactory("source_port") {}
};

class DirectSourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DirectSourceIPInputFactory
    : public BaseFactory<
          DirectSourceIPInput,
          envoy::extensions::matching::common_inputs::network::v3::DirectSourceIPInput> {
public:
  DirectSourceIPInputFactory() : BaseFactory("direct_source_ip") {}
};

class SourceTypeInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceTypeInputFactory
    : public BaseFactory<SourceTypeInput,
                         envoy::extensions::matching::common_inputs::network::v3::SourceTypeInput> {
public:
  SourceTypeInputFactory() : BaseFactory("source_type") {}
};

class ServerNameInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ServerNameInputFactory
    : public BaseFactory<ServerNameInput,
                         envoy::extensions::matching::common_inputs::network::v3::ServerNameInput> {
public:
  ServerNameInputFactory() : BaseFactory("server_name") {}
};

class TransportProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class TransportProtocolInputFactory
    : public BaseFactory<
          TransportProtocolInput,
          envoy::extensions::matching::common_inputs::network::v3::TransportProtocolInput> {
public:
  TransportProtocolInputFactory() : BaseFactory("transport_protocol") {}
};

class ApplicationProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ApplicationProtocolInputFactory
    : public BaseFactory<
          ApplicationProtocolInput,
          envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput> {
public:
  ApplicationProtocolInputFactory() : BaseFactory("application_protocol") {}
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
