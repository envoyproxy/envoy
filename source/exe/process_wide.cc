#include "exe/process_wide.h"

#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/http/http2/nghttp2.h"

#include "server/proto_descriptors.h"

#include "ares.h"

namespace Envoy {
namespace {
// Static variable to count initialization pairs. For tests like
// main_common_test, we need to count to avoid double initialization or
// shutdown.
uint32_t process_wide_initialized;
} // namespace

ProcessWide::ProcessWide() : initialization_depth_(process_wide_initialized) {
  if (process_wide_initialized++ == 0) {
    ares_library_init(ARES_LIB_INIT_ALL);
    Event::Libevent::Global::initialize();
    Envoy::Server::validateProtoDescriptors();
    Http::Http2::initializeNghttp2Logging();

    // We do not initialize Google gRPC here -- we instead instantiate
    // Grpc::GoogleGrpcContext in MainCommon immediately after instantiating
    // ProcessWide. This is because ProcessWide is instantiated in the unit-test
    // flow in test/test_runner.h, and grpc_init() instantiates threads which
    // allocate memory asynchronous to running tests, making it hard to
    // accurately measure memory consumption, and making unit-test debugging
    // non-deterministic. See https://github.com/envoyproxy/envoy/issues/8282
    // for details. Of course we also need grpc_init called in unit-tests that
    // test Google gRPC, and the relevant classes must also instantiate
    // Grpc::GoogleGrpcContext, which allows for nested instantiation.
    //
    // It appears that grpc_init() started instantiating threads in grpc 1.22.1,
    // which was integrated in https://github.com/envoyproxy/envoy/pull/8196,
    // around the time the flakes in #8282 started being reported.
  }
}

ProcessWide::~ProcessWide() {
  ASSERT(process_wide_initialized > 0);
  if (--process_wide_initialized == 0) {
    process_wide_initialized = false;
    ares_library_cleanup();
  }
  ASSERT(process_wide_initialized == initialization_depth_);
}

} // namespace Envoy
