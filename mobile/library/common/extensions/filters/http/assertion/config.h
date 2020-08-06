#include <string>

#include "extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/assertion/filter.pb.h"
#include "library/common/extensions/filters/http/assertion/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

/**
 * Config registration for the assertion filter. @see NamedHttpFilterConfigFactory.
 */
class AssertionFilterFactory
    : public Common::FactoryBase<envoymobile::extensions::filters::http::assertion::Assertion> {
public:
  AssertionFilterFactory() : FactoryBase("assertion") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::assertion::Assertion& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(AssertionFilterFactory);

} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
