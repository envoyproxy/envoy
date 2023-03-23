#include "source/extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void ExecuteFilterAction::createFilters(Http::FilterChainFactoryCallbacks& callbacks) const {
  cb_(callbacks);
}
REGISTER_FACTORY(ExecuteFilterActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);

void ExecuteFilterMultiAction::createFilters(std::function<void(Envoy::Http::FilterFactoryCb&)> parse_wrapper) const {
  for (auto cb : cb_list_) {
    parse_wrapper(cb);
  }
}
REGISTER_FACTORY(ExecuteFilterMultiActionFactory,
                 Matcher::ActionFactory<Http::Matching::HttpFilterActionContext>);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
