#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/socket_tag/filter.pb.h"
#include "library/common/extensions/filters/http/socket_tag/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketTag {

/**
 * Config registration for the socket tag filter. @see NamedHttpFilterConfigFactory.
 */
class SocketTagFilterFactory : public Common::UnifiedFactoryBase<
                                   envoymobile::extensions::filters::http::socket_tag::SocketTag> {
public:
  SocketTagFilterFactory() : UnifiedFactoryBase("socket_tag") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::socket_tag::SocketTag& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(SocketTagFilterFactory);

} // namespace SocketTag
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
