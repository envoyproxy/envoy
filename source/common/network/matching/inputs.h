#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/type/matcher/v3/network_inputs.pb.h"
#include "envoy/type/matcher/v3/network_inputs.pb.validate.h"

namespace Envoy {
namespace Network {
namespace Matching {

class DestinationIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DestinationIPInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "destination-ip"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<DestinationIPInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::DestinationIPInput>();
  }
};

class DestinationPortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DestinationPortInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "destination-port"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<DestinationPortInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::DestinationPortInput>();
  }
};

class SourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceIPInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "source-ip"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SourceIPInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::SourceIPInput>();
  }
};

class SourcePortInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourcePortInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "source-port"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SourcePortInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::SourcePortInput>();
  }
};

class DirectSourceIPInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class DirectSourceIPInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "direct-source-ip"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<DirectSourceIPInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::DirectSourceIPInput>();
  }
};

class SourceTypeInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class SourceTypeInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "source-type"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<SourceTypeInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::SourceTypeInput>();
  }
};

class ServerNameInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ServerNameInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "server-name"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ServerNameInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::ServerNameInput>();
  }
};

class TransportProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class TransportProtocolInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "transport-protocol"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<TransportProtocolInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::TransportProtocolInput>();
  }
};

class ApplicationProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ApplicationProtocolInputFactory : public Matcher::DataInputFactory<MatchingData> {
public:
  std::string name() const override { return "application-protocol"; }

  Matcher::DataInputFactoryCb<MatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ApplicationProtocolInput>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::ApplicationProtocolInput>();
  }
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
