#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

REGISTER_FACTORY(ConversionActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);
REGISTER_FACTORY(DropActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
