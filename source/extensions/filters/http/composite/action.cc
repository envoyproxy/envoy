#include "extensions/filters/http/composite/action.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
REGISTER_FACTORY(CompositeActionFactory, Matcher::ActionFactory);
}}}}