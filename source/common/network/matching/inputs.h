#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/network_inputs.pb.h"
#include "envoy/type/matcher/v3/network_inputs.pb.validate.h"

#include "source/common/network/matching/data.h"

#include "data.h"

namespace Envoy {
namespace Network {
namespace Matching {

class IpDataInputBase : public Matcher::DataInput<NetworkMatchingData> {
public:
  explicit IpDataInputBase() = default;

  virtual const Address::CidrRange& select(const NetworkMatchingData& data) const PURE;

  Matcher::DataInputGetResult get(const NetworkMatchingData& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            select(data).asString()};
  }
};

template <class DataInputType, class ProtoType>
class IpDataInputFactoryBase : public Matcher::DataInputFactory<NetworkMatchingData> {
public:
  explicit IpDataInputFactoryBase(const std::string& name) : name_(name) {}

  std::string name() const override { return name_; }

  Matcher::DataInputFactoryCb<NetworkMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<DataInputType>(); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

class SourceIpDataInput : public IpDataInputBase {
public:
  explicit SourceIpDataInput() = default;

  const Address::CidrRange& select(const NetworkMatchingData& data) const override {
    return data.sourceIp();
  }
};

class SourceIpDataInputFactory
    : public IpDataInputFactoryBase<SourceIpDataInput,
                                    envoy::type::matcher::v3::SourceIpMatchInput> {
public:
  SourceIpDataInputFactory() : IpDataInputFactoryBase("source-ip") {}
};

class DestinationIpDataInput : public IpDataInputBase {
public:
  explicit DestinationIpDataInput() = default;

  const Address::CidrRange& select(const NetworkMatchingData& data) const override {
    return data.destinationIp();
  }
};

class DestinationIpDataInputFactory
    : public IpDataInputFactoryBase<DestinationIpDataInput,
                                    envoy::type::matcher::v3::DestinationIpMatchInput> {
public:
  DestinationIpDataInputFactory() : IpDataInputFactoryBase("destination-ip") {}
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
