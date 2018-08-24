#include <memory>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/exception.h"
#include "envoy/service/discovery/v2/sds.pb.h"

#include "common/secret/sds_api.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace Envoy {
namespace Secret {
namespace {

class SdsApiTest : public testing::Test {};

// Validate that SdsApi object is created and initialized successfully.
TEST_F(SdsApiTest, BasicTest) {
  ::testing::InSequence s;
  const envoy::service::discovery::v2::SdsDummy dummy;
  NiceMock<Server::MockInstance> server;
  NiceMock<Init::MockManager> init_manager;
  EXPECT_CALL(init_manager, registerTarget(_));

  envoy::api::v2::core::ConfigSource config_source;
  config_source.mutable_api_config_source()->set_api_type(
      envoy::api::v2::core::ApiConfigSource::GRPC);
  auto grpc_service = config_source.mutable_api_config_source()->add_grpc_services();
  auto google_grpc = grpc_service->mutable_google_grpc();
  google_grpc->set_target_uri("fake_address");
  google_grpc->set_stat_prefix("test");
  SdsApi sds_api(server.localInfo(), server.dispatcher(), server.random(), server.stats(),
                 server.clusterManager(), init_manager, config_source, "abc.com", []() {});

  NiceMock<Grpc::MockAsyncClient>* grpc_client{new NiceMock<Grpc::MockAsyncClient>()};
  NiceMock<Grpc::MockAsyncClientFactory>* factory{new NiceMock<Grpc::MockAsyncClientFactory>()};
  EXPECT_CALL(server.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
      .WillOnce(Invoke([factory](const envoy::api::v2::core::GrpcService&, Stats::Scope&, bool) {
        return Grpc::AsyncClientFactoryPtr{factory};
      }));
  EXPECT_CALL(*factory, create()).WillOnce(Invoke([grpc_client] {
    return Grpc::AsyncClientPtr{grpc_client};
  }));
  EXPECT_CALL(init_manager.initialized_, ready());
  init_manager.initialize();
}

// Validate that SdsApi updates secrets successfully if a good secret is passed to onConfigUpdate().
TEST_F(SdsApiTest, SecretUpdateSuccess) {
  NiceMock<Server::MockInstance> server;
  NiceMock<Init::MockManager> init_manager;
  envoy::api::v2::core::ConfigSource config_source;
  SdsApi sds_api(server.localInfo(), server.dispatcher(), server.random(), server.stats(),
                 server.clusterManager(), init_manager, config_source, "abc.com", []() {});

  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle =
      sds_api.addUpdateCallback([&secret_callback]() { secret_callback.onAddOrUpdateSecret(); });

  std::string yaml =
      R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
    )EOF";

  Protobuf::RepeatedPtrField<envoy::api::v2::auth::Secret> secret_resources;
  auto secret_config = secret_resources.Add();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config);
  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  sds_api.onConfigUpdate(secret_resources, "");

  const std::string cert_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            sds_api.secret()->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            sds_api.secret()->privateKey());

  handle->remove();
}

// Validate that SdsApi throws exception if an empty secret is passed to onConfigUpdate().
TEST_F(SdsApiTest, EmptyResource) {
  NiceMock<Server::MockInstance> server;
  NiceMock<Init::MockManager> init_manager;
  envoy::api::v2::core::ConfigSource config_source;
  SdsApi sds_api(server.localInfo(), server.dispatcher(), server.random(), server.stats(),
                 server.clusterManager(), init_manager, config_source, "abc.com", []() {});

  Protobuf::RepeatedPtrField<envoy::api::v2::auth::Secret> secret_resources;

  EXPECT_THROW_WITH_MESSAGE(sds_api.onConfigUpdate(secret_resources, ""), EnvoyException,
                            "Missing SDS resources for abc.com in onConfigUpdate()");
}

// Validate that SdsApi throws exception if multiple secrets are passed to onConfigUpdate().
TEST_F(SdsApiTest, SecretUpdateWrongSize) {
  NiceMock<Server::MockInstance> server;
  NiceMock<Init::MockManager> init_manager;
  envoy::api::v2::core::ConfigSource config_source;
  SdsApi sds_api(server.localInfo(), server.dispatcher(), server.random(), server.stats(),
                 server.clusterManager(), init_manager, config_source, "abc.com", []() {});

  std::string yaml =
      R"EOF(
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
      )EOF";

  Protobuf::RepeatedPtrField<envoy::api::v2::auth::Secret> secret_resources;
  auto secret_config_1 = secret_resources.Add();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config_1);
  auto secret_config_2 = secret_resources.Add();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config_2);

  EXPECT_THROW_WITH_MESSAGE(sds_api.onConfigUpdate(secret_resources, ""), EnvoyException,
                            "Unexpected SDS secrets length: 2");
}

// Validate that SdsApi throws exception if secret name passed to onConfigUpdate()
// does not match configured name.
TEST_F(SdsApiTest, SecretUpdateWrongSecretName) {
  NiceMock<Server::MockInstance> server;
  NiceMock<Init::MockManager> init_manager;
  envoy::api::v2::core::ConfigSource config_source;
  SdsApi sds_api(server.localInfo(), server.dispatcher(), server.random(), server.stats(),
                 server.clusterManager(), init_manager, config_source, "abc.com", []() {});

  std::string yaml =
      R"EOF(
      name: "wrong.name.com"
      tls_certificate:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/ssl/test_data/selfsigned_key.pem"
        )EOF";

  Protobuf::RepeatedPtrField<envoy::api::v2::auth::Secret> secret_resources;
  auto secret_config = secret_resources.Add();
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config);

  EXPECT_THROW_WITH_MESSAGE(sds_api.onConfigUpdate(secret_resources, ""), EnvoyException,
                            "Unexpected SDS secret (expecting abc.com): wrong.name.com");
}

} // namespace
} // namespace Secret
} // namespace Envoy
