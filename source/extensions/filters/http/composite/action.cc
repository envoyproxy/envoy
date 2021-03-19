#include "extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
absl::Status CompositeAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  // We make use of exceptions here because we don't control the callbacks interface, so we have to
  // bubble up errors without being able to change the interface.
  try {
    cb_(callbacks);
  } catch (EnvoyException e) {
    return absl::InternalError(fmt::format("error creating filter: {}", e.what()));
  }

  return absl::OkStatus();
}
REGISTER_FACTORY(CompositeActionFactory, Matcher::ActionFactory);
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy