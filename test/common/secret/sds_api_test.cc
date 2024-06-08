#include <memory>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/secret/sds_api.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"
#include "source/extensions/config_subscription/filesystem/filesystem_subscription_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Throw;

namespace Envoy {
namespace Secret {
namespace {

class SdsApiTestBase {
protected:
  SdsApiTestBase() {
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");
  }

  void initialize() { init_target_handle_->initialize(init_watcher_); }
  void setupMocks() {
    EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
  }

  Api::ApiPtr api_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Config::MockSubscriptionFactory> subscription_factory_;
  NiceMock<Init::MockManager> init_manager_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Event::GlobalTimeSystem time_system_;
  Init::TargetHandlePtr init_target_handle_;
  Event::DispatcherPtr dispatcher_;
  Stats::TestUtil::TestStore stats_;
};

class SdsApiTest : public testing::Test, public SdsApiTestBase {};

// Validate that SdsApi object is created and initialized successfully.
TEST_F(SdsApiTest, BasicTest) {
  ::testing::InSequence s;
  const envoy::service::secret::v3::SdsDummy dummy;

  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());
  initialize();
}

// Validate that a noop init manager is used if the InitManger passed into the constructor
// has been already initialized. This is a regression test for
// https://github.com/envoyproxy/envoy/issues/12013
TEST_F(SdsApiTest, InitManagerInitialised) {
  std::string sds_config =
      R"EOF(
  resources:
    - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
      name: "abc.com"
      tls_certificate:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
     )EOF";

  const std::string sds_config_path = TestEnvironment::writeStringToFileForTest(
      "sds.yaml", TestEnvironment::substitute(sds_config), false);
  NiceMock<Config::MockSubscriptionCallbacks> callbacks;
  Config::OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<TestUtility::TestOpaqueResourceDecoderImpl<
          envoy::extensions::transport_sockets::tls::v3::Secret>>("name"));
  Config::SubscriptionStats stats(Config::Utility::generateStats(*stats_.rootScope()));
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  envoy::config::core::v3::ConfigSource config_source;

  EXPECT_CALL(subscription_factory_, subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([this, &sds_config_path, &resource_decoder,
                        &stats](const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                Stats::Scope&, Config::SubscriptionCallbacks& cbs,
                                Config::OpaqueResourceDecoderSharedPtr,
                                const Config::SubscriptionOptions&) {
        return std::make_unique<Config::FilesystemSubscriptionImpl>(
            *dispatcher_, Config::makePathConfigSource(sds_config_path), cbs, resource_decoder,
            stats, validation_visitor_, *api_);
      }));

  auto init_manager = Init::ManagerImpl("testing");
  auto noop_init_target =
      Init::TargetImpl(fmt::format("noop test init target"), [] { /*Do nothing.*/ });
  init_manager.add(noop_init_target);
  auto noop_watcher = Init::WatcherImpl(fmt::format("noop watcher"), []() { /*Do nothing.*/ });
  init_manager.initialize(noop_watcher);

  EXPECT_EQ(Init::Manager::State::Initializing, init_manager.state());
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  EXPECT_NO_THROW(init_manager.add(*sds_api.initTarget()));
}

// Validate that bad ConfigSources are caught at construction time. This is a regression test for
// https://github.com/envoyproxy/envoy/issues/10976.
TEST_F(SdsApiTest, BadConfigSource) {
  ::testing::InSequence s;
  envoy::config::core::v3::ConfigSource config_source;
  EXPECT_CALL(subscription_factory_, subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(InvokeWithoutArgs([]() {
        throw EnvoyException("bad config");
        return nullptr;
      }));
  EXPECT_THROW_WITH_MESSAGE(TlsCertificateSdsApi(
                                config_source, "abc.com", subscription_factory_, time_system_,
                                validation_visitor_, stats_, []() {}, *dispatcher_, *api_),
                            EnvoyException, "bad config");
}

// Validate that TlsCertificateSdsApi updates secrets successfully if a good secret
// is passed to onConfigUpdate().
TEST_F(SdsApiTest, DynamicTlsCertificateUpdateSuccess) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());
  initialize();
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle = sds_api.addUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });

  std::string yaml =
      R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    )EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});

  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  auto tls_config = Ssl::TlsCertificateConfigImpl::create(*sds_api.secret(), ctx, *api_).value();
  const std::string cert_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config->privateKey());
}

class SdsRotationApiTest : public SdsApiTestBase {
protected:
  SdsRotationApiTest() {
    api_ = Api::createApiForTest(filesystem_);
    setupMocks();
    EXPECT_CALL(filesystem_, splitPathFromFilename(_))
        .WillRepeatedly(
            Invoke([](absl::string_view path) -> absl::StatusOr<Filesystem::PathSplitResult> {
              return Filesystem::fileSystemForTest().splitPathFromFilename(path);
            }));
  }

  Secret::MockSecretCallbacks secret_callback_;
  Common::CallbackHandlePtr handle_;
  std::vector<Filesystem::Watcher::OnChangedCb> watch_cbs_;
  Event::MockDispatcher mock_dispatcher_;
  Filesystem::MockInstance filesystem_;
};

class TlsCertificateSdsRotationApiTest : public testing::TestWithParam<bool>,
                                         public SdsRotationApiTest {
protected:
  TlsCertificateSdsRotationApiTest()
      : watched_directory_(GetParam()), cert_path_("/foo/bar/cert.pem"),
        key_path_("/foo/bar/key.pem"), expected_watch_path_("/foo/bar/"), trigger_path_("/foo") {
    envoy::config::core::v3::ConfigSource config_source;
    sds_api_ = std::make_unique<TlsCertificateSdsApi>(
        config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
        []() {}, mock_dispatcher_, *api_);
    init_manager_.add(*sds_api_->initTarget());
    initialize();
    handle_ =
        sds_api_->addUpdateCallback([this]() { return secret_callback_.onAddOrUpdateSecret(); });
  }

  void onConfigUpdate(const std::string& cert_value, const std::string& key_value) {
    const std::string yaml = fmt::format(
        R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{}"
    private_key:
      filename: "{}"
    )EOF",
        cert_path_, key_path_);
    envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
    TestUtility::loadFromYaml(yaml, typed_secret);
    if (watched_directory_) {
      typed_secret.mutable_tls_certificate()->mutable_watched_directory()->set_path(trigger_path_);
    }
    const auto decoded_resources = TestUtility::decodeResources({typed_secret});

    auto* watcher = new Filesystem::MockWatcher();
    if (watched_directory_) {
      EXPECT_CALL(mock_dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
      EXPECT_CALL(*watcher, addWatch(trigger_path_ + "/", Filesystem::Watcher::Events::MovedTo, _))
          .WillOnce(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                watch_cbs_.push_back(cb);
                return absl::OkStatus();
              }));
      EXPECT_CALL(filesystem_, fileReadToEnd(cert_path_)).WillOnce(Return(cert_value));
      EXPECT_CALL(filesystem_, fileReadToEnd(key_path_)).WillOnce(Return(key_value));
      EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
    } else {
      EXPECT_CALL(filesystem_, fileReadToEnd(cert_path_)).WillOnce(Return(cert_value));
      EXPECT_CALL(filesystem_, fileReadToEnd(key_path_)).WillOnce(Return(key_value));
      EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
      EXPECT_CALL(mock_dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
      EXPECT_CALL(*watcher, addWatch(expected_watch_path_, Filesystem::Watcher::Events::MovedTo, _))
          .Times(2)
          .WillRepeatedly(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                watch_cbs_.push_back(cb);
                return absl::OkStatus();
              }));
    }
    EXPECT_TRUE(
        subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }

  const bool watched_directory_;
  std::string cert_path_;
  std::string key_path_;
  std::string expected_watch_path_;
  std::string trigger_path_;
  std::unique_ptr<TlsCertificateSdsApi> sds_api_;
};

INSTANTIATE_TEST_SUITE_P(TlsCertificateSdsRotationApiTestParams, TlsCertificateSdsRotationApiTest,
                         testing::Values(false, true));

class CertificateValidationContextSdsRotationApiTest : public testing::TestWithParam<bool>,
                                                       public SdsRotationApiTest {
protected:
  CertificateValidationContextSdsRotationApiTest() {
    envoy::config::core::v3::ConfigSource config_source;
    sds_api_ = std::make_unique<CertificateValidationContextSdsApi>(
        config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
        []() {}, mock_dispatcher_, *api_);
    init_manager_.add(*sds_api_->initTarget());
    initialize();
    handle_ =
        sds_api_->addUpdateCallback([this]() { return secret_callback_.onAddOrUpdateSecret(); });
  }

  void onConfigUpdate(const std::string& trusted_ca_path, const std::string& trusted_ca_value,
                      const std::string& crl_path, const std::string& crl_value,
                      const std::string& watch_path) {
    const std::string yaml = fmt::format(
        R"EOF(
  name: "abc.com"
  validation_context:
    trusted_ca:
      filename: "{}"
    crl:
      filename: "{}"
    allow_expired_certificate: true
    )EOF",
        trusted_ca_path, crl_path);
    envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
    TestUtility::loadFromYaml(yaml, typed_secret);
    const auto decoded_resources = TestUtility::decodeResources({typed_secret});

    auto* watcher = new Filesystem::MockWatcher();
    EXPECT_CALL(filesystem_, fileReadToEnd(trusted_ca_path)).WillOnce(Return(trusted_ca_value));
    EXPECT_CALL(filesystem_, fileReadToEnd(crl_path)).WillOnce(Return(crl_value));
    EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
    EXPECT_CALL(mock_dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
    EXPECT_CALL(*watcher, addWatch(watch_path, Filesystem::Watcher::Events::MovedTo, _))
        .WillOnce(Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
          watch_cbs_.push_back(cb);
          return absl::OkStatus();
        }));
    EXPECT_CALL(*watcher, addWatch(watch_path, Filesystem::Watcher::Events::MovedTo, _))
        .WillOnce(Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
          watch_cbs_.push_back(cb);
          return absl::OkStatus();
        }));
    EXPECT_TRUE(
        subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());
  }

  std::unique_ptr<CertificateValidationContextSdsApi> sds_api_;
};

INSTANTIATE_TEST_SUITE_P(CertificateValidationContextSdsRotationApiTestParams,
                         CertificateValidationContextSdsRotationApiTest,
                         testing::Values(false, true));

// Initial onConfigUpdate() of TlsCertificate secret.
TEST_P(TlsCertificateSdsRotationApiTest, InitialUpdate) {
  InSequence s;
  onConfigUpdate("a", "b");

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("a", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("b", secret.private_key().inline_bytes());
}

// Two distinct updates with onConfigUpdate() of TlsCertificate secret.
TEST_P(TlsCertificateSdsRotationApiTest, MultiUpdate) {
  InSequence s;
  onConfigUpdate("a", "b");
  {
    const auto& secret = *sds_api_->secret();
    EXPECT_EQ("a", secret.certificate_chain().inline_bytes());
    EXPECT_EQ("b", secret.private_key().inline_bytes());
  }

  cert_path_ = "/new/foo/bar/cert.pem";
  key_path_ = "/new/foo/bar/key.pem";
  expected_watch_path_ = "/new/foo/bar/";
  onConfigUpdate("c", "d");
  {
    const auto& secret = *sds_api_->secret();
    EXPECT_EQ("c", secret.certificate_chain().inline_bytes());
    EXPECT_EQ("d", secret.private_key().inline_bytes());
  }
}

// Watch trigger without file change has no effect.
TEST_P(TlsCertificateSdsRotationApiTest, NopWatchTrigger) {
  InSequence s;
  onConfigUpdate("a", "b");

  for (const auto& cb : watch_cbs_) {
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("a"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("b"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("a"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("b"));
    EXPECT_TRUE(cb(Filesystem::Watcher::Events::MovedTo).ok());
  }

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("a", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("b", secret.private_key().inline_bytes());
}

// Basic rotation of TlsCertificate.
TEST_P(TlsCertificateSdsRotationApiTest, RotationWatchTrigger) {
  InSequence s;
  onConfigUpdate("a", "b");

  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
  EXPECT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  if (!watched_directory_) {
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
    EXPECT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  }

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("c", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("d", secret.private_key().inline_bytes());
}

// Failed rotation of TlsCertificate.
TEST_P(TlsCertificateSdsRotationApiTest, FailedRotation) {
  InSequence s;
  onConfigUpdate("a", "b");

  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem"))
      .WillOnce(Throw(EnvoyException("fail")));
  EXPECT_LOG_CONTAINS("warn", "Failed to reload certificates: ",
                      EXPECT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok()));
  EXPECT_EQ(1U, stats_.counter("sds.abc.com.key_rotation_failed").value());

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("a", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("b", secret.private_key().inline_bytes());
}

// Basic rotation of CertificateValidationContext.
TEST_P(CertificateValidationContextSdsRotationApiTest, CertificateValidationContext) {
  InSequence s;
  onConfigUpdate("/foo/bar/ca.pem", "a", "/foo/bar/crl.pem", "b", "/foo/bar/");

  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/ca.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/crl.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/ca.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/crl.pem")).WillOnce(Return("d"));

  EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
  EXPECT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("c", secret.trusted_ca().inline_bytes());
  EXPECT_EQ("d", secret.crl().inline_bytes());
}

// Hash consistency verification prevents races.
TEST_P(TlsCertificateSdsRotationApiTest, RotationConsistency) {
  InSequence s;
  onConfigUpdate("a", "b");

  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("a"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
  EXPECT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  if (!watched_directory_) {
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
    EXPECT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  }

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("c", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("d", secret.private_key().inline_bytes());
}

// Hash consistency verification failure, no callback.
TEST_P(TlsCertificateSdsRotationApiTest, RotationConsistencyExhaustion) {
  InSequence s;
  onConfigUpdate("a", "b");

  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("a"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("c"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("e"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("f"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("d"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("f"));
  EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("g"));
  // We've exhausted the bounded retries, but continue with the non-atomic rotation.
  EXPECT_CALL(secret_callback_, onAddOrUpdateSecret());
  EXPECT_LOG_CONTAINS(
      "warn", "Unable to atomically refresh secrets due to > 5 non-atomic rotations observed",
      EXPECT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok()));
  if (!watched_directory_) {
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("f"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("g"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/cert.pem")).WillOnce(Return("f"));
    EXPECT_CALL(filesystem_, fileReadToEnd("/foo/bar/key.pem")).WillOnce(Return("g"));
    EXPECT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  }

  const auto& secret = *sds_api_->secret();
  EXPECT_EQ("f", secret.certificate_chain().inline_bytes());
  EXPECT_EQ("g", secret.private_key().inline_bytes());
}

class PartialMockSds : public SdsApi {
public:
  PartialMockSds(Stats::Store& stats, NiceMock<Init::MockManager>& init_manager,
                 envoy::config::core::v3::ConfigSource& config_source,
                 Config::SubscriptionFactory& subscription_factory, TimeSource& time_source,
                 Event::Dispatcher& dispatcher, Api::Api& api)
      : SdsApi(
            config_source, "abc.com", subscription_factory, time_source, validation_visitor_, stats,
            []() {}, dispatcher, api) {
    init_manager.add(init_target_);
  }

  MOCK_METHOD(absl::Status, onConfigUpdate,
              (const std::vector<Config::DecodedResourceRef>&, const std::string&));
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added,
                              const Protobuf::RepeatedPtrField<std::string>& removed,
                              const std::string& version) override {
    return SdsApi::onConfigUpdate(added, removed, version);
  }
  void setSecret(const envoy::extensions::transport_sockets::tls::v3::Secret&) override {}
  void validateConfig(const envoy::extensions::transport_sockets::tls::v3::Secret&) override {}
  std::vector<std::string> getDataSourceFilenames() override { return {}; }
  Config::WatchedDirectory* getWatchedDirectory() override { return nullptr; }

  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Basic test of delta's passthrough call to the state-of-the-world variant, to
// increase coverage.
TEST_F(SdsApiTest, Delta) {
  auto secret = std::make_unique<envoy::extensions::transport_sockets::tls::v3::Secret>();
  secret->set_name("secret_1");
  Config::DecodedResourceImpl resource(std::move(secret), "name", {}, "version1");
  std::vector<Config::DecodedResourceRef> resources{resource};

  envoy::config::core::v3::ConfigSource config_source;
  Event::GlobalTimeSystem time_system;
  setupMocks();
  PartialMockSds sds(stats_, init_manager_, config_source, subscription_factory_, time_system,
                     *dispatcher_, *api_);
  initialize();
  EXPECT_CALL(sds, onConfigUpdate(DecodedResourcesEq(resources), "version1"));
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate(resources, {}, "ignored").ok());

  // An attempt to remove a resource while adding a resource should not error.
  auto secret_again = std::make_unique<envoy::extensions::transport_sockets::tls::v3::Secret>();
  secret_again->set_name("secret_1");
  Config::DecodedResourceImpl resource_v2(std::move(secret_again), "name", {}, "version2");
  std::vector<Config::DecodedResourceRef> resources_v2{resource_v2};
  Protobuf::RepeatedPtrField<std::string> removals;
  *removals.Add() = "route_0";
  EXPECT_TRUE(
      subscription_factory_.callbacks_->onConfigUpdate(resources_v2, removals, "ignored").ok());
  removals.RemoveLast();

  // An attempt to remove a resource without adding another resource should also not error
  *removals.Add() = "route_1";
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate({}, removals, "ignored").ok());
}

// Tests SDS's use of the delta variant of onConfigUpdate().
TEST_F(SdsApiTest, DeltaUpdateSuccess) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle = sds_api.addUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });

  std::string yaml =
      R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    )EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});

  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  initialize();
  EXPECT_TRUE(
      subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "").ok());

  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> ctx;
  auto tls_config = Ssl::TlsCertificateConfigImpl::create(*sds_api.secret(), ctx, *api_).value();
  const std::string cert_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            tls_config->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            tls_config->privateKey());
}

// Validate that CertificateValidationContextSdsApi updates secrets successfully if
// a good secret is passed to onConfigUpdate().
TEST_F(SdsApiTest, DynamicCertificateValidationContextUpdateSuccess) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  CertificateValidationContextSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle = sds_api.addUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });

  std::string yaml =
      R"EOF(
  name: "abc.com"
  validation_context:
    trusted_ca: { filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem" }
    allow_expired_certificate: true
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  initialize();
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());

  auto cvc_config =
      Ssl::CertificateValidationContextConfigImpl::create(*sds_api.secret(), *api_).value();
  const std::string ca_cert = "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(ca_cert)),
            cvc_config->caCert());
}

class CvcValidationCallback {
public:
  virtual ~CvcValidationCallback() = default;
  virtual void validateCvc(
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&) PURE;
};

class MockCvcValidationCallback : public CvcValidationCallback {
public:
  MockCvcValidationCallback() = default;
  ~MockCvcValidationCallback() override = default;
  MOCK_METHOD(void, validateCvc,
              (const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&));
};

// Validate that CertificateValidationContextSdsApi updates secrets successfully if
// a good secret is passed to onConfigUpdate(), and that merged CertificateValidationContext
// provides correct information.
TEST_F(SdsApiTest, DefaultCertificateValidationContextTest) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  CertificateValidationContextSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle = sds_api.addUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  NiceMock<MockCvcValidationCallback> validation_callback;
  auto validation_handle = sds_api.addValidationCallback(
      [&validation_callback](
          const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext& cvc) {
        validation_callback.validateCvc(cvc);
        return absl::OkStatus();
      });

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  typed_secret.set_name("abc.com");
  auto* dynamic_cvc = typed_secret.mutable_validation_context();
  dynamic_cvc->set_allow_expired_certificate(false);
  dynamic_cvc->mutable_trusted_ca()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));
  auto* san_matcher = dynamic_cvc->add_match_typed_subject_alt_names();
  san_matcher->mutable_matcher()->set_exact("second san");
  san_matcher->set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
  const std::string dynamic_verify_certificate_spki =
      "QGJRPdmx/r5EGOFLb2MTiZp2isyC0Whht7iazhzXaCM=";
  dynamic_cvc->add_verify_certificate_spki(dynamic_verify_certificate_spki);
  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  EXPECT_CALL(validation_callback, validateCvc(_));

  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  initialize();
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());

  const std::string default_verify_certificate_hash =
      "0000000000000000000000000000000000000000000000000000000000000000";
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext default_cvc;
  default_cvc.set_allow_expired_certificate(true);
  default_cvc.mutable_trusted_ca()->set_inline_bytes("fake trusted ca");
  san_matcher = default_cvc.add_match_typed_subject_alt_names();
  san_matcher->mutable_matcher()->set_exact("first san");
  san_matcher->set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
  default_cvc.add_verify_certificate_hash(default_verify_certificate_hash);
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext merged_cvc =
      default_cvc;
  merged_cvc.MergeFrom(*sds_api.secret());
  auto cvc_config = Ssl::CertificateValidationContextConfigImpl::create(merged_cvc, *api_).value();
  // Verify that merging CertificateValidationContext applies logical OR to bool
  // field.
  EXPECT_TRUE(cvc_config->allowExpiredCertificate());
  // Verify that singular fields are overwritten.
  const std::string ca_cert = "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(ca_cert)),
            cvc_config->caCert());
  // Verify that repeated fields are concatenated.
  EXPECT_EQ(2, cvc_config->subjectAltNameMatchers().size());
  EXPECT_EQ("first san", cvc_config->subjectAltNameMatchers()[0].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS,
            cvc_config->subjectAltNameMatchers()[0].san_type());
  EXPECT_EQ("second san", cvc_config->subjectAltNameMatchers()[1].matcher().exact());
  EXPECT_EQ(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS,
            cvc_config->subjectAltNameMatchers()[1].san_type());
  // Verify that if dynamic CertificateValidationContext does not set certificate hash list, the new
  // secret contains hash list from default CertificateValidationContext.
  EXPECT_EQ(1, cvc_config->verifyCertificateHashList().size());
  EXPECT_EQ(default_verify_certificate_hash, cvc_config->verifyCertificateHashList()[0]);
  // Verify that if default CertificateValidationContext does not set certificate SPKI list, the new
  // secret contains SPKI list from dynamic CertificateValidationContext.
  EXPECT_EQ(1, cvc_config->verifyCertificateSpkiList().size());
  EXPECT_EQ(dynamic_verify_certificate_spki, cvc_config->verifyCertificateSpkiList()[0]);
}

class GenericSecretValidationCallback {
public:
  virtual ~GenericSecretValidationCallback() = default;
  virtual void
  validateGenericSecret(const envoy::extensions::transport_sockets::tls::v3::GenericSecret&) PURE;
};

class MockGenericSecretValidationCallback : public GenericSecretValidationCallback {
public:
  MockGenericSecretValidationCallback() = default;
  ~MockGenericSecretValidationCallback() override = default;
  MOCK_METHOD(void, validateGenericSecret,
              (const envoy::extensions::transport_sockets::tls::v3::GenericSecret&));
};

// Validate that GenericSecretSdsApi updates secrets successfully if
// a good secret is passed to onConfigUpdate().
TEST_F(SdsApiTest, GenericSecretSdsApiTest) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  GenericSecretSdsApi sds_api(
      config_source, "encryption_key", subscription_factory_, time_system_, validation_visitor_,
      stats_, []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  auto handle = sds_api.addUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  NiceMock<MockGenericSecretValidationCallback> validation_callback;
  auto validation_handle = sds_api.addValidationCallback(
      [&validation_callback](
          const envoy::extensions::transport_sockets::tls::v3::GenericSecret& secret) {
        validation_callback.validateGenericSecret(secret);
        return absl::OkStatus();
      });

  std::string yaml =
      R"EOF(
name: "encryption_key"
generic_secret:
  secret:
    filename: "{{ test_rundir }}/test/common/tls/test_data/aes_128_key"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});
  EXPECT_CALL(secret_callback, onAddOrUpdateSecret());
  EXPECT_CALL(validation_callback, validateGenericSecret(_));
  initialize();
  EXPECT_TRUE(subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());

  const envoy::extensions::transport_sockets::tls::v3::GenericSecret generic_secret(
      *sds_api.secret());
  const std::string secret_path = "{{ test_rundir }}/test/common/tls/test_data/aes_128_key";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(secret_path)),
            Config::DataSource::read(generic_secret.secret(), true, *api_).value());
}

// Validate that SdsApi throws exception if an empty secret is passed to onConfigUpdate().
TEST_F(SdsApiTest, EmptyResource) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  initialize();
  EXPECT_EQ(subscription_factory_.callbacks_->onConfigUpdate({}, "").message(),
            "Missing SDS resources for abc.com in onConfigUpdate()");
}

// Validate that SdsApi throws exception if multiple secrets are passed to onConfigUpdate().
TEST_F(SdsApiTest, SecretUpdateWrongSize) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  std::string yaml =
      R"EOF(
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
      )EOF";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret, typed_secret});

  initialize();
  EXPECT_EQ(
      subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
      "Unexpected SDS secrets length for abc.com, number of added resources "
      "2, number of removed resources 0. Expected sum is 1");
  Protobuf::RepeatedPtrField<std::string> unused;
  EXPECT_EQ(subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, unused, "")
                .message(),
            "Unexpected SDS secrets length for abc.com, number of added resources "
            "2, number of removed resources 0. Expected sum is 1");
}

// Validate that SdsApi throws exception if secret name passed to onConfigUpdate()
// does not match configured name.
TEST_F(SdsApiTest, SecretUpdateWrongSecretName) {
  envoy::config::core::v3::ConfigSource config_source;
  setupMocks();
  TlsCertificateSdsApi sds_api(
      config_source, "abc.com", subscription_factory_, time_system_, validation_visitor_, stats_,
      []() {}, *dispatcher_, *api_);
  init_manager_.add(*sds_api.initTarget());

  std::string yaml =
      R"EOF(
      name: "wrong.name.com"
      tls_certificate:
        certificate_chain:
          filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
        private_key:
          filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
        )EOF";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), typed_secret);
  const auto decoded_resources = TestUtility::decodeResources({typed_secret});

  initialize();
  EXPECT_EQ(
      subscription_factory_.callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
      "Unexpected SDS secret (expecting abc.com): wrong.name.com");
}

} // namespace
} // namespace Secret
} // namespace Envoy
