#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/local_error/filter.pb.h"
#include "library/common/extensions/filters/http/local_error/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalError {

/**
 * Config registration for the local_error filter. @see NamedHttpFilterConfigFactory.
 */
class LocalErrorFilterFactory
    : public Common::FactoryBase<envoymobile::extensions::filters::http::local_error::LocalError> {
public:
  LocalErrorFilterFactory() : FactoryBase("local_error") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::local_error::LocalError& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(LocalErrorFilterFactory);

} // namespace LocalError
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
