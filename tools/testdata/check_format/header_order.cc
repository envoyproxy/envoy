#include <signal.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/config/bootstrap/v2//bootstrap.pb.validate.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/api/api_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/utility.h"
#include "source/common/config/resources.h"
#include "source/common/config/utility.h"
#include "source/common/local_info/local_info_impl.h"
#include "source/common/memory/stats.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/rds_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/upstream/cluster_manager_impl.h"
#include "source/common/version/version.h"
#include "source/server/configuration_impl.h"
#include "source/server/connection_handler_impl.h"
#include "source/server/guarddog_impl.h"
#include "source/server/listener_hooks.h"

#include "absl/types/optional.h"

namespace Envoy {

// Something awesome goes here.

} // namespace Envoy
