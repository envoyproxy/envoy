#include "test/mocks/router/upstream_request.h"

namespace Envoy {
namespace Router {

MockUpstreamRequest::MockUpstreamRequest(RouterFilterInterface& router_interface,
                                         std::unique_ptr<GenericConnPool>&& conn_pool)
    : UpstreamRequest(router_interface, std::move(conn_pool), false, true, true) {}

MockUpstreamRequest::~MockUpstreamRequest() = default;

} // namespace Router
} // namespace Envoy
