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

ProcessWide::ProcessWide() {
#ifdef ENVOY_GOOGLE_GRPC
  grpc_init();
#endif
  ares_library_init(ARES_LIB_INIT_ALL);
  Event::Libevent::Global::initialize();
  RELEASE_ASSERT(Envoy::Server::validateProtoDescriptors(), "");
  Http::Http2::initializeNghttp2Logging();
}

ProcessWide::~ProcessWide() {
  ares_library_cleanup();
#ifdef ENVOY_GOOGLE_GRPC
  grpc_shutdown();
#endif
}

} // namespace Envoy
