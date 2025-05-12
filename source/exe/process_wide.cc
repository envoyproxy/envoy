#include "source/exe/process_wide.h"

#include "envoy/network/dns_resolver.h"

#include "source/common/common/assert.h"
#include "source/common/event/libevent.h"
#include "source/common/http/http2/nghttp2.h"
#include "source/server/proto_descriptors.h"

namespace Envoy {
namespace {

struct InitData {
  uint32_t count_ ABSL_GUARDED_BY(mutex_){};
  absl::Mutex mutex_;
};

// Static variable to count initialization pairs. For tests like
// main_common_test, we need to count to avoid double initialization or
// shutdown.
InitData& processWideInitData() { MUTABLE_CONSTRUCT_ON_FIRST_USE(InitData); };
} // namespace

ProcessWide::ProcessWide(bool validate_proto_descriptors) {
  // Note that the following lock has the dual use of making sure that initialization is complete
  // before a second caller can enter and leave this function.
  auto& init_data = processWideInitData();
  absl::MutexLock lock(&init_data.mutex_);

  if (init_data.count_++ == 0) {
    // TODO(mattklein123): Audit the following as not all of these have to be re-initialized in the
    // edge case where something does init/destroy/init/destroy.
    Event::Libevent::Global::initialize();
#if defined(ENVOY_ENABLE_FULL_PROTOS)
    if (validate_proto_descriptors) {
      Envoy::Server::validateProtoDescriptors();
    }
#else
    UNREFERENCED_PARAMETER(validate_proto_descriptors);
#endif
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
  auto& init_data = processWideInitData();
  absl::MutexLock lock(&init_data.mutex_);

  ASSERT(init_data.count_ > 0);
  if (--init_data.count_ == 0) {
    Network::DnsResolverFactory::terminateFactories();
  }
}

} // namespace Envoy
