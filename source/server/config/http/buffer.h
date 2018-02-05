#pragma once

#include <string>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

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
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::buffer::v2::Buffer()};
  }

  std::string name() override { return Config::HttpFilterNames::get().BUFFER; }

private:
  HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::buffer::v2::Buffer& proto_config,
               const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
