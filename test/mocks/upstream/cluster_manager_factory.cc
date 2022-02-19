#include "cluster_manager_factory.h"

namespace Envoy {
// Overriding Server::Instance PrintTo to enable the class forward declaration.
namespace Server {
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const Instance& server, std::ostream* os) { *os << &server; }
} // namespace Server

namespace Upstream {
MockClusterManagerFactory::MockClusterManagerFactory() = default;

MockClusterManagerFactory::~MockClusterManagerFactory() = default;
} // namespace Upstream
} // namespace Envoy
