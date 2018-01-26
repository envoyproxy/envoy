#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/http/buffer.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the buffer filter. @see NamedHttpFilterConfigFactory.
 */
class BufferFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::http::Buffer()};
  }

  std::string name() override { return Config::HttpFilterNames::get().BUFFER; }

private:
  HttpFilterFactoryCb createFilter(const envoy::api::v2::filter::http::Buffer& proto_config,
                                   const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
