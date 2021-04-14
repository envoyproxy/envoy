#include "extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}
REGISTER_FACTORY(ExecuteFilterActionFactory, Matcher::ActionFactory);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
