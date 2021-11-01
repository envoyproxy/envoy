#pragma once

#include <string>

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/network_inputs.pb.h"
#include "envoy/type/matcher/v3/network_inputs.pb.validate.h"

namespace Envoy {
namespace Network {
namespace Matching {
/**
 * Common base class for all the IP DataInputs.
 */
class IpDataInputBase : public Matcher::DataInput<NetworkMatchingData> {
public:
  explicit IpDataInputBase() = default;

  virtual OptRef<const Address::Ip> select(const NetworkMatchingData& data) const PURE;

  Matcher::DataInputGetResult get(const NetworkMatchingData& data) const override {
    const auto ip = select(data);

    if (!ip.has_value()) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt};
    }

    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, ip->addressAsString()};
  }
};

/**
 * Common base class for all the IP DataInputsFactory.
 */
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

  OptRef<const Address::Ip> select(const NetworkMatchingData& data) const override {
    return data.sourceIp();
  }
};

class SourceIpDataInputFactory
    : public IpDataInputFactoryBase<SourceIpDataInput,
                                    envoy::type::matcher::v3::SourceIpMatchInput> {
public:
  SourceIpDataInputFactory() : IpDataInputFactoryBase("source-ip") {}
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
