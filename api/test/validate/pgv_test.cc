// NOLINT(namespace-envoy)
#include <cstdlib>
#include <iostream>

// We don't use all the headers in the test below, but including them anyway as
// a cheap way to get some C++ compiler sanity checking.
#include "envoy/api/v2/cluster.pb.validate.h"
#include "envoy/api/v2/endpoint.pb.validate.h"
#include "envoy/api/v2/listener.pb.validate.h"
#include "envoy/api/v2/route.pb.validate.h"
#include "envoy/api/v2/core/protocol.pb.validate.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.validate.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.validate.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"
#include "envoy/extensions/filters/http/health_check/v3/health_check.pb.validate.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.validate.h"
#include "envoy/extensions/filters/http/lua/v3/lua.pb.validate.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/extensions/filters/http/squash/v3/squash.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/redis/v3/redis.pb.validate.h"
#include "envoy/api/v2/listener/listener.pb.validate.h"
#include "envoy/api/v2/route/route.pb.validate.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.validate.h"

#include "google/protobuf/text_format.h"

template <class Proto> struct TestCase {
  void run() {
    std::string err;
    if (Validate(invalid_message, &err)) {
      std::cerr << "Unexpected successful validation of invalid message: "
                << invalid_message.DebugString() << std::endl;
      exit(EXIT_FAILURE);
    }
    if (!Validate(valid_message, &err)) {
      std::cerr << "Unexpected failed validation of valid message: " << valid_message.DebugString()
                << ", " << err << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  Proto& invalid_message;
  Proto& valid_message;
};

// Basic protoc-gen-validate C++ validation header inclusion and Validate calls
// from data plane API.
int main(int argc, char* argv[]) {
  envoy::config::bootstrap::v2::Bootstrap invalid_bootstrap;
  invalid_bootstrap.mutable_static_resources()->add_clusters();
  // This is a baseline test of the validation features we care about. It's
  // probably not worth adding in every filter and field that we want to valid
  // in the API upfront, but as regressions occur, this is the place to add the
  // specific case.
  const std::string valid_bootstrap_text = R"EOF(
  node {}
  cluster_manager {}
  admin {
    access_log_path: "/dev/null"
    address { pipe { path: "/" } }
  }
  )EOF";
  envoy::config::bootstrap::v2::Bootstrap valid_bootstrap;
  if (!google::protobuf::TextFormat::ParseFromString(valid_bootstrap_text, &valid_bootstrap)) {
    std::cerr << "Unable to parse text proto: " << valid_bootstrap_text << std::endl;
    exit(EXIT_FAILURE);
  }
  TestCase<envoy::config::bootstrap::v2::Bootstrap>{invalid_bootstrap, valid_bootstrap}.run();

  exit(EXIT_SUCCESS);
}
