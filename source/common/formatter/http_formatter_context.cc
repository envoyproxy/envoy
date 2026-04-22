#include "envoy/formatter/http_formatter_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Formatter {

static constexpr absl::string_view DEFAULT_FORMAT =
    "[%START_TIME%] "
    "\"%REQUEST_HEADER(:METHOD)% %REQUEST_HEADER(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% "
    "%RESPONSE_FLAGS% "
    "%BYTES_RECEIVED% "
    "%BYTES_SENT% "
    "%DURATION% "
    "%RESPONSE_HEADER(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQUEST_HEADER(X-FORWARDED-FOR)%\" "
    "\"%REQUEST_HEADER(USER-AGENT)%\" "
    "\"%REQUEST_HEADER(X-REQUEST-ID)%\" "
    "\"%REQUEST_HEADER(:AUTHORITY)%\" "
    "\"%UPSTREAM_HOST%\"\n";

absl::StatusOr<FormatterPtr> HttpSubstitutionFormatUtils::defaultSubstitutionFormatter() {
  // It is possible that failed to parse the default format string if the required formatters
  // are compiled out.
  return Envoy::Formatter::FormatterImpl::create(DEFAULT_FORMAT, false);
}

} // namespace Formatter
} // namespace Envoy
