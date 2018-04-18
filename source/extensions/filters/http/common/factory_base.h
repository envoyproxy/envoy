#pragma once

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Common base class for HTTP filter factory registrations. Removes a substantial amount of
 * boilerplate.
 */
template <class ConfigProto>
class FactoryBase : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  // Server::Configuration::NamedHttpFilterConfigFactory
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object&, const std::string&,
                      Server::Configuration::FactoryContext&) override {
    // Only used in v1 filters.
    NOT_IMPLEMENTED;
  }

  Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createTypedFilterFactoryFromProto(
        MessageUtil::downcastAndValidate<const ConfigProto&>(proto_config), stats_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ConfigProto()};
  }

  std::string name() override { return name_; }

protected:
  FactoryBase(const std::string& name) : name_(name) {}

private:
  virtual Server::Configuration::HttpFilterFactoryCb
  createTypedFilterFactoryFromProto(const ConfigProto& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
