#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/type/matcher/v3/network_inputs.pb.h"
#include "envoy/type/matcher/v3/network_inputs.pb.validate.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <class InputType, class ProtoType>
class BaseFactory : public Matcher::DataInputFactory<MatchingData> {
protected:
  explicit BaseFactory(const std::string& name) : name_(name) {}

public:
  std::string name() const override { return name_; }

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
    : public BaseFactory<DestinationIPInput, envoy::type::matcher::v3::DestinationIPInput> {
public:
  DestinationIPInputFactory() : BaseFactory("destination-ip") {}
};

class DestinationPortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DestinationPortInputFactory
    : public BaseFactory<DestinationPortInput, envoy::type::matcher::v3::DestinationPortInput> {
public:
  DestinationPortInputFactory() : BaseFactory("destination-port") {}
};

class SourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceIPInputFactory
    : public BaseFactory<SourceIPInput, envoy::type::matcher::v3::SourceIPInput> {
public:
  SourceIPInputFactory() : BaseFactory("source-ip") {}
};

class SourcePortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourcePortInputFactory
    : public BaseFactory<SourcePortInput, envoy::type::matcher::v3::SourcePortInput> {
public:
  SourcePortInputFactory() : BaseFactory("source-port") {}
};

class DirectSourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DirectSourceIPInputFactory
    : public BaseFactory<DirectSourceIPInput, envoy::type::matcher::v3::DirectSourceIPInput> {
public:
  DirectSourceIPInputFactory() : BaseFactory("direct-source-ip") {}
};

class SourceTypeInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceTypeInputFactory
    : public BaseFactory<SourceTypeInput, envoy::type::matcher::v3::SourceTypeInput> {
public:
  SourceTypeInputFactory() : BaseFactory("source-type") {}
};

class ServerNameInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ServerNameInputFactory
    : public BaseFactory<ServerNameInput, envoy::type::matcher::v3::ServerNameInput> {
public:
  ServerNameInputFactory() : BaseFactory("server-name") {}
};

class TransportProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class TransportProtocolInputFactory
    : public BaseFactory<TransportProtocolInput, envoy::type::matcher::v3::TransportProtocolInput> {
public:
  TransportProtocolInputFactory() : BaseFactory("transport-protocol") {}
};

class ApplicationProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ApplicationProtocolInputFactory
    : public BaseFactory<ApplicationProtocolInput,
                         envoy::type::matcher::v3::ApplicationProtocolInput> {
public:
  ApplicationProtocolInputFactory() : BaseFactory("application-protocol") {}
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
