#include "mock.h"

#include "source/extensions/filters/network/mysql_proxy/mysql_session.h"
#include "source/extensions/filters/network/mysql_proxy/route.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MockRouter::MockRouter(RouteSharedPtr route) : route(route) {
  ON_CALL(*this, upstreamPool(_)).WillByDefault(Return(route));
  ON_CALL(*this, defaultPool()).WillByDefault(Return(route));
}

MockRoute::MockRoute(Upstream::ThreadLocalCluster* instance, const std::string& name)
    : pool(instance), cluster_name(name) {
  ON_CALL(*this, upstream()).WillByDefault(Return(pool));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(cluster_name));
}

MockDecoder::MockDecoder() {
  session = std::make_unique<MySQLSession>();
  ON_CALL(*this, getSession()).WillByDefault([&]() -> MySQLSession& { return *session; });
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
