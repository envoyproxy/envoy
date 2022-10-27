#include "source/extensions/matching/actions/format_string/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

REGISTER_FACTORY(ActionFactory,
                 Matcher::ActionFactory<Server::FilterChainActionFactoryContext>);

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
