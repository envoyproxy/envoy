#pragma once

#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.h"
#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {

/**
 * Config registration for TcpPostFilter. @see NamedHttpFilterConfigFactory.
 */
class TcpPostFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::tcp_post::v3::TcpPost> {
public:
  TcpPostFilterFactory() : FactoryBase(HttpFilterNames::get().TcpPost) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::tcp_post::v3::TcpPost& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
