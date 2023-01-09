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
class SocketTagFilterFactory
    : public Common::FactoryBase<envoymobile::extensions::filters::http::socket_tag::SocketTag> {
public:
  SocketTagFilterFactory() : FactoryBase("socket_tag") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::socket_tag::SocketTag& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(SocketTagFilterFactory);

} // namespace SocketTag
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
