#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

/**
 * Config registration for http filters that have empty configuration blocks.
 * The boiler plate instantiation functions (createFilterFactory, createFilterFactoryFromProto,
 * and createEmptyConfigProto) are implemented here. Users of this class have to implement
 * the createFilter function that instantiates the actual filter.
 */
class EmptyHttpFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  virtual Http::FilterFactoryCb createFilter(const std::string& stat_prefix,
                                             Server::Configuration::FactoryContext& context) PURE;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string configType() override {
    // Prevent registration of filters by type. This is only allowed in tests.
    return "";
  }

  std::string name() const override { return name_; }

protected:
  EmptyHttpFilterConfig(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
