// NOLINT(namespace-envoy)
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/context_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

....useless stuff...

    NiceMock<Upstream::MockClusterManager>
        cm_;

... uninteresting stuff..