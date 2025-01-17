#pragma once

#include "source/common/protobuf/utility.h"

#include "contrib/sip_proxy/filters/network/source/filters/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace SipFilters {

template <class ConfigProto> class FactoryBase : public NamedSipFilterConfigFactory {
public:
  FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilterFactoryFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                                 proto_config, context.messageValidationVisitor()),
                                             stats_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  const std::string name_;
};

} // namespace SipFilters
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
