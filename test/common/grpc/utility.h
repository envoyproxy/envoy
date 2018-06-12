#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Grpc {

class TestUtility {
public:
  static void setTestSslGoogleGrpcConfig(envoy::api::v2::core::GrpcService& config,
                                         bool use_client_cert) {
    auto* google_grpc = config.mutable_google_grpc();
    auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
    ssl_creds->mutable_root_certs()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    if (use_client_cert) {
      ssl_creds->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      ssl_creds->mutable_cert_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    }
  }
};

} // namespace Grpc
} // namespace Envoy
