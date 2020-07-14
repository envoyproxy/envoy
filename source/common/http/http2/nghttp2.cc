#include "common/http/http2/nghttp2.h"

#include "common/common/logger.h"

// nghttp2 fails to convey the POSIX ssize_t declaration
// that Microsoft declines to implement. Pick up a valid
// ssize_t declaration for win32 in our platform.h
#include "envoy/common/platform.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

void initializeNghttp2Logging() {
  // Event when ENVOY_NGHTTP2_TRACE is not set, we install a debug logger, to prevent nghttp2
  // logging directly to stdout at -l trace.
  nghttp2_set_debug_vprintf_callback([](const char* format, va_list args) {
    if (std::getenv("ENVOY_NGHTTP2_TRACE") != nullptr) {
      char buf[2048];
      const int n = ::vsnprintf(buf, sizeof(buf), format, args);
      // nghttp2 inserts new lines, but we also insert a new line in the ENVOY_LOG
      // below, so avoid double \n.
      if (n >= 1 && static_cast<size_t>(n) < sizeof(buf) && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
      }
      ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::http2), trace, "nghttp2: {}", buf);
    }
  });
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
