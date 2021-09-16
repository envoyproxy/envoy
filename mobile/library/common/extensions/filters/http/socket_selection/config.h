#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/socket_selection/filter.pb.h"
#include "library/common/extensions/filters/http/socket_selection/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SocketSelection {

/**
 * Config registration for the socket_selection filter. @see NamedHttpFilterConfigFactory.
 */
class SocketSelectionFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::socket_selection::SocketSelection> {
public:
  SocketSelectionFilterFactory() : FactoryBase("socket_selection") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::socket_selection::SocketSelection& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(SocketSelectionFilterFactory);

} // namespace SocketSelection
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
