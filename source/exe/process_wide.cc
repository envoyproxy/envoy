#include "exe/process_wide.h"

#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/http/http2/nghttp2.h"

#include "server/proto_descriptors.h"

#include "ares.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "grpc/grpc.h"
#endif

namespace Envoy {
namespace {
// Static variable to count initialization pairs. For tests like
// main_common_test, we need to count to avoid double initialization or
// shutdown.
uint32_t process_wide_initialized;
} // namespace

ProcessWide::ProcessWide() : initialization_depth_(process_wide_initialized) {
  if (process_wide_initialized++ == 0) {
#ifdef ENVOY_GOOGLE_GRPC
    grpc_init();
#endif
    ares_library_init(ARES_LIB_INIT_ALL);
    Event::Libevent::Global::initialize();
    Envoy::Server::validateProtoDescriptors();
    Http::Http2::initializeNghttp2Logging();
  }
}

ProcessWide::~ProcessWide() {
  ASSERT(process_wide_initialized > 0);
  if (--process_wide_initialized == 0) {
    process_wide_initialized = false;
    ares_library_cleanup();
#ifdef ENVOY_GOOGLE_GRPC
    grpc_shutdown();
#endif
  }
  ASSERT(process_wide_initialized == initialization_depth_);
}

} // namespace Envoy
