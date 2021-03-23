#include "extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void CompositeAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}
REGISTER_FACTORY(CompositeActionFactory, Matcher::ActionFactory);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy