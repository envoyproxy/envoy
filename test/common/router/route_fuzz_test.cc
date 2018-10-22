#include "envoy/api/v2/rds.pb.validate.h"

#include "common/router/config_impl.h"

#include "test/common/router/route_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Router {

// TODO(htuch): figure out how to generate via a genrule from config_impl_test the full corpus.
DEFINE_PROTO_FUZZER(const test::common::router::RouteTestCase& input) {
  try {
    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    NiceMock<Server::Configuration::MockFactoryContext> factory_context;
    MessageUtil::validate(input.config());
    ConfigImpl config(input.config(), factory_context, true);
    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(input.headers());
    // It's a precondition of routing that {:authority, :path, x-forwarded-proto} headers exists,
    // HCM enforces this.
    if (headers.Host() == nullptr) {
      headers.insertHost().value(std::string("example.com"));
    }
    if (headers.Path() == nullptr) {
      headers.insertPath().value(std::string("/"));
    }
    if (headers.ForwardedProto() == nullptr) {
      headers.insertForwardedProto().value(std::string("http"));
    }
    auto route = config.route(headers, input.random_value());
    if (route != nullptr && route->routeEntry() != nullptr) {
      route->routeEntry()->finalizeRequestHeaders(headers, stream_info, true);
    }
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace Router
} // namespace Envoy
