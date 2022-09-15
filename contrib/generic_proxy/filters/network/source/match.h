#pragma once

#include <functional>
#include <memory>

#include "source/common/matcher/matcher.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/matcher/v3/matcher.pb.h"
#include "contrib/envoy/extensions/filters/network/generic_proxy/matcher/v3/matcher.pb.validate.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ServiceDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::ServiceMatchInput;
using MethodDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::MethodMatchInput;
using PropertyDataInputProto =
    envoy::extensions::filters::network::generic_proxy::matcher::v3::PropertyMatchInput;

class ServiceMatchDataInput : public Matcher::DataInput<Request> {
public:
  Matcher::DataInputGetResult get(const Request& data) const override {
    Matcher::DataInputGetResult result;
    result.data_availability_ = Matcher::DataInputGetResult::DataAvailability::AllDataAvailable;
    result.data_.emplace(data.host());
    return result;
  }
};

class ServiceMatchDataInputFactory : public Matcher::DataInputFactory<Request> {
public:
  ServiceMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ServiceDataInputProto>();
  }

  Matcher::DataInputFactoryCb<Request>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<ServiceMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.service"; }
};

class MethodMatchDataInput : public Matcher::DataInput<Request> {
public:
  Matcher::DataInputGetResult get(const Request& data) const override {
    Matcher::DataInputGetResult result;
    result.data_availability_ = Matcher::DataInputGetResult::DataAvailability::AllDataAvailable;
    result.data_.emplace(data.method());
    return result;
  }
};

class MethodMatchDataInputFactory : public Matcher::DataInputFactory<Request> {
public:
  MethodMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<MethodDataInputProto>();
  }

  Matcher::DataInputFactoryCb<Request>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<MethodMatchDataInput>(); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.method"; }
};

class PropertyMatchDataInput : public Matcher::DataInput<Request> {
public:
  PropertyMatchDataInput(const std::string& property_name) : name_(property_name) {}

  Matcher::DataInputGetResult get(const Request& data) const override {
    Matcher::DataInputGetResult result;
    result.data_availability_ = Matcher::DataInputGetResult::DataAvailability::AllDataAvailable;

    const auto value = data.getByKey(name_);

    if (value.has_value()) {
      result.data_.emplace(value.value());
    }
    return result;
  }

private:
  const std::string name_;
};

class PropertyMatchDataInputFactory : public Matcher::DataInputFactory<Request> {
public:
  PropertyMatchDataInputFactory() = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<PropertyDataInputProto>();
  }

  Matcher::DataInputFactoryCb<Request>
  createDataInputFactoryCb(const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& visitor) override {
    const auto& config =
        MessageUtil::downcastAndValidate<const PropertyDataInputProto&>(message, visitor);
    const std::string name = config.property_name();

    return [name]() { return std::make_unique<PropertyMatchDataInput>(name); };
  }

  std::string name() const override { return "envoy.matching.generic_proxy.input.property"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
