#include "envoy/formatter/http_formatter_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Formatter {

static constexpr absl::string_view DEFAULT_FORMAT =
    "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" "
    "%RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% "
    "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "
    "\"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" "
    "\"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n";

absl::StatusOr<FormatterPtr> HttpSubstitutionFormatUtils::defaultSubstitutionFormatter() {
  // It is possible that failed to parse the default format string if the required formatters
  // are compiled out.
  return Envoy::Formatter::FormatterImpl::create(DEFAULT_FORMAT, false);
}

} // namespace Formatter
} // namespace Envoy
