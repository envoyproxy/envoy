#pragma once

#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.h"
#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"
#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

/**
 * Config registration for the reverse_conn filter. @see NamedHttpFilterConfigFactory.
 */
class ReverseConnFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::reverse_conn::v3::ReverseConn> {
public:
  ReverseConnFilterConfigFactory() : FactoryBase("reverse_conn") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::reverse_conn::v3::ReverseConn& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
