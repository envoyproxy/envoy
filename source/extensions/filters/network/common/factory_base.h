#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {

/**
 * Common base class for network filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto, class ProtocolOptionsProto = ConfigProto>
class FactoryBase : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // Server::Configuration::NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Server::Configuration::FactoryContext&) override {
    // Only used in v1 filters.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config), context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return std::make_unique<ProtocolOptionsProto>();
  }

  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& proto_config) override {
    return createProtocolOptionsTyped(
        MessageUtil::downcastAndValidate<const ProtocolOptionsProto&>(proto_config));
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    Server::Configuration::FactoryContext& context) PURE;

  virtual Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsTyped(const ProtocolOptionsProto&) {
    throw EnvoyException(fmt::format("filter {} does not support protocol options", name_));
  }

  const std::string name_;
};

} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
