#include "test/integration/base_integration_test.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/event/libevent.h"
#include "source/common/network/utility.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"
#include "source/server/proto_descriptors.h"

#include "test/integration/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
envoy::config::bootstrap::v3::Bootstrap configToBootstrap(const std::string& config) {
#ifdef ENVOY_ENABLE_YAML
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(config, bootstrap);
  return bootstrap;
#else
  UNREFERENCED_PARAMETER(config);
  PANIC("YAML support compiled out: can't parse YAML");
#endif
}

using ::testing::_;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::Invoke;
using ::testing::IsSubstring;
using ::testing::NiceMock;
using ::testing::ReturnRef;

BaseIntegrationTest::BaseIntegrationTest(const InstanceConstSharedPtrFn& upstream_address_fn,
                                         Network::Address::IpVersion version,
                                         const envoy::config::bootstrap::v3::Bootstrap& bootstrap)
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      mock_buffer_factory_(new NiceMock<MockBufferFactory>),
      dispatcher_(api_->allocateDispatcher("test_thread",
                                           Buffer::WatermarkFactoryPtr{mock_buffer_factory_})),
      version_(version), upstream_address_fn_(upstream_address_fn),
      config_helper_(version, bootstrap),
      default_log_level_(TestEnvironment::getOptions().logLevel()) {
  Envoy::Server::validateProtoDescriptors();
  // This is a hack, but there are situations where we disconnect fake upstream connections and
  // then we expect the server connection pool to get the disconnect before the next test starts.
  // This does not always happen. This pause should allow the server to pick up the disconnect
  // notification and clear the pool connection if necessary. A real fix would require adding fairly
  // complex test hooks to the server and/or spin waiting on stats, neither of which I think are
  // necessary right now.
  timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(10));
  ON_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
      .WillByDefault(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                               std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));
  ON_CALL(factory_context_.server_context_, api()).WillByDefault(ReturnRef(*api_));
  ON_CALL(factory_context_, statsScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));

#ifndef ENVOY_ADMIN_FUNCTIONALITY
  config_helper_.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void { bootstrap.clear_admin(); });
#endif
}

BaseIntegrationTest::BaseIntegrationTest(const InstanceConstSharedPtrFn& upstream_address_fn,
                                         Network::Address::IpVersion version,
                                         const std::string& config)
    : BaseIntegrationTest(upstream_address_fn, version, configToBootstrap(config)) {}

const BaseIntegrationTest::InstanceConstSharedPtrFn
BaseIntegrationTest::defaultAddressFunction(Network::Address::IpVersion version) {
  return [version](int) {
    return Network::Utility::parseInternetAddress(Network::Test::getLoopbackAddressString(version),
                                                  0);
  };
}

BaseIntegrationTest::BaseIntegrationTest(Network::Address::IpVersion version,
                                         const std::string& config)
    : BaseIntegrationTest(defaultAddressFunction(version), version, config) {}

Network::ClientConnectionPtr BaseIntegrationTest::makeClientConnection(uint32_t port) {
  return makeClientConnectionWithOptions(port, nullptr);
}

Network::ClientConnectionPtr BaseIntegrationTest::makeClientConnectionWithOptions(
    uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) {
  Network::ClientConnectionPtr connection(dispatcher_->createClientConnection(
      Network::Utility::resolveUrl(
          fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port)),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), options,
      nullptr));

  connection->enableHalfClose(enableHalfClose());
  return connection;
}

void BaseIntegrationTest::initialize() {
  RELEASE_ASSERT(!initialized_, "");
  RELEASE_ASSERT(Event::Libevent::Global::initialized(), "");
  initialized_ = true;

  createUpstreams();
  createXdsUpstream();
  createEnvoy();

#ifdef ENVOY_ADMIN_FUNCTIONALITY
  if (!skip_tag_extraction_rule_check_) {
    checkForMissingTagExtractionRules();
  }
#endif
}

Network::DownstreamTransportSocketFactoryPtr
BaseIntegrationTest::createUpstreamTlsContext(const FakeUpstreamConfig& upstream_config) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  const std::string rundir = TestEnvironment::runfilesDirectory();
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_trusted_ca()
      ->set_filename(rundir + "/test/config/integration/certs/cacert.pem");
  auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
  certs->mutable_certificate_chain()->set_filename(
      rundir + "/test/config/integration/certs/upstreamcert.pem");
  certs->mutable_private_key()->set_filename(rundir +
                                             "/test/config/integration/certs/upstreamkey.pem");

  if (upstream_config.upstream_protocol_ == Http::CodecType::HTTP2) {
    tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");
  } else if (upstream_config.upstream_protocol_ == Http::CodecType::HTTP1) {
    tls_context.mutable_common_tls_context()->add_alpn_protocols("http/1.1");
  }
  if (upstream_config.upstream_protocol_ != Http::CodecType::HTTP3) {
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);
    static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
        std::vector<std::string>{});
  } else {
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
    quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);

    std::vector<std::string> server_names;
    auto& config_factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::DownstreamTransportSocketConfigFactory>(
        "envoy.transport_sockets.quic");
    return config_factory.createTransportSocketFactory(quic_config, factory_context_, server_names);
  }
}

void BaseIntegrationTest::createUpstreams() {
  for (uint32_t i = 0; i < fake_upstreams_count_; ++i) {
    auto endpoint = upstream_address_fn_(i);
    createUpstream(endpoint, upstreamConfig());
  }
}
void BaseIntegrationTest::createUpstream(Network::Address::InstanceConstSharedPtr endpoint,
                                         FakeUpstreamConfig& config) {
  Network::DownstreamTransportSocketFactoryPtr factory =
      upstream_tls_ ? createUpstreamTlsContext(config)
                    : Network::Test::createRawBufferDownstreamSocketFactory();
  if (autonomous_upstream_) {
    fake_upstreams_.emplace_back(std::make_unique<AutonomousUpstream>(
        std::move(factory), endpoint, config, autonomous_allow_incomplete_streams_));
  } else {
    fake_upstreams_.emplace_back(
        std::make_unique<FakeUpstream>(std::move(factory), endpoint, config));
  }
}

std::string BaseIntegrationTest::finalizeConfigWithPorts(ConfigHelper& config_helper,
                                                         std::vector<uint32_t>& ports,
                                                         bool use_lds) {
  if (use_lds) {
    ENVOY_LOG_MISC(debug, "Setting up file-based LDS");
    // Before finalization, set up a real lds path, replacing the default /dev/null
    std::string lds_path = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
    config_helper.addConfigModifier(
        [lds_path](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          bootstrap.mutable_dynamic_resources()->mutable_lds_config()->set_resource_api_version(
              envoy::config::core::v3::V3);
          bootstrap.mutable_dynamic_resources()
              ->mutable_lds_config()
              ->mutable_path_config_source()
              ->set_path(lds_path);
        });
  }

  // Note that finalize assumes that every fake_upstream_ must correspond to a bootstrap config
  // static entry. So, if you want to manually create a fake upstream without specifying it in the
  // config, you will need to do so *after* initialize() (which calls this function) is done.
  config_helper.finalize(ports);

  envoy::config::bootstrap::v3::Bootstrap bootstrap = config_helper.bootstrap();
  if (use_lds) {
    // After the config has been finalized, write the final listener config to the lds file.
    const std::string lds_path =
        config_helper.bootstrap().dynamic_resources().lds_config().path_config_source().path();
    envoy::service::discovery::v3::DiscoveryResponse lds;
    lds.set_version_info("0");
    for (auto& listener : config_helper.bootstrap().static_resources().listeners()) {
      ProtobufWkt::Any* resource = lds.add_resources();
      resource->PackFrom(listener);
    }
#ifdef ENVOY_ENABLE_YAML
    TestEnvironment::writeStringToFileForTest(
        lds_path, MessageUtil::getJsonStringFromMessageOrError(lds), true);
#else
    PANIC("YAML support compiled out: can't parse YAML");
#endif
    // Now that the listeners have been written to the lds file, remove them from static resources
    // or they will not be reloadable.
    bootstrap.mutable_static_resources()->mutable_listeners()->Clear();
  }
#ifdef ENVOY_ENABLE_YAML
  ENVOY_LOG_MISC(debug, "Running Envoy with configuration:\n{}",
                 MessageUtil::getYamlStringFromMessage(bootstrap));
#else
  ENVOY_LOG_MISC(debug, "Running Envoy with configuration:\n{}", bootstrap.DebugString());
#endif

  const std::string bootstrap_path = TestEnvironment::writeStringToFileForTest(
      "bootstrap.pb", TestUtility::getProtobufBinaryStringFromMessage(bootstrap));
  return bootstrap_path;
}

void BaseIntegrationTest::createEnvoy() {
  std::vector<uint32_t> ports;
  for (auto& upstream : fake_upstreams_) {
    if (upstream->localAddress()->ip()) {
      ports.push_back(upstream->localAddress()->ip()->port());
    }
  }

  const std::string bootstrap_path = finalizeConfigWithPorts(config_helper_, ports, use_lds_);

  std::vector<std::string> named_ports;
  const auto& static_resources = config_helper_.bootstrap().static_resources();
  named_ports.reserve(static_resources.listeners_size());
  for (int i = 0; i < static_resources.listeners_size(); ++i) {
    named_ports.push_back(static_resources.listeners(i).name());
  }
  createGeneratedApiTestServer(bootstrap_path, named_ports, {false, true, false}, false);
}

void BaseIntegrationTest::setUpstreamProtocol(Http::CodecType protocol) {
  upstream_config_.upstream_protocol_ = protocol;
  if (upstream_config_.upstream_protocol_ == Http::CodecType::HTTP2) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  } else if (upstream_config_.upstream_protocol_ == Http::CodecType::HTTP1) {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  } else {
    RELEASE_ASSERT(protocol == Http::CodecType::HTTP3, "");
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(false, true);
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          // Docker doesn't allow writing to the v6 address returned by
          // Network::Utility::getLocalAddress.
          if (version_ == Network::Address::IpVersion::v6) {
            auto* bind_config_address = bootstrap.mutable_static_resources()
                                            ->mutable_clusters(0)
                                            ->mutable_upstream_bind_config()
                                            ->mutable_source_address();
            bind_config_address->set_address("::1");
            bind_config_address->set_port_value(0);
          }

          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          protocol_options.mutable_explicit_http_config()->mutable_http3_protocol_options();
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }
}

absl::optional<uint64_t> BaseIntegrationTest::waitForNextRawUpstreamConnection(
    const std::vector<uint64_t>& upstream_indices, FakeRawConnectionPtr& fake_upstream_connection,
    std::chrono::milliseconds connection_wait_timeout) {
  AssertionResult result = AssertionFailure();
  int upstream_index = 0;
  Event::TestTimeSystem::RealTimeBound bound(connection_wait_timeout);
  // Loop over the upstreams until the call times out or an upstream request is
  // received.
  while (!result) {
    upstream_index = upstream_index % upstream_indices.size();
    result = fake_upstreams_[upstream_indices[upstream_index]]->waitForRawConnection(
        fake_upstream_connection, std::chrono::milliseconds(5));
    if (result) {
      return upstream_index;
    } else if (!bound.withinBound()) {
      RELEASE_ASSERT(0, "Timed out waiting for new connection.");
      break;
    }
    ++upstream_index;
  }
  RELEASE_ASSERT(result, result.message());
  return {};
}

IntegrationTcpClientPtr
BaseIntegrationTest::makeTcpConnection(uint32_t port,
                                       const Network::ConnectionSocket::OptionsSharedPtr& options,
                                       Network::Address::InstanceConstSharedPtr source_address,
                                       absl::string_view destination_address) {
  return std::make_unique<IntegrationTcpClient>(*dispatcher_, *mock_buffer_factory_, port, version_,
                                                enableHalfClose(), options, source_address,
                                                destination_address);
}

void BaseIntegrationTest::registerPort(const std::string& key, uint32_t port) {
  port_map_[key] = port;
}

uint32_t BaseIntegrationTest::lookupPort(const std::string& key) {
  auto it = port_map_.find(key);
  if (it != port_map_.end()) {
    return it->second;
  }
  RELEASE_ASSERT(
      false,
      fmt::format("lookupPort() called on service type '{}', which has not been added to port_map_",
                  key));
}

void BaseIntegrationTest::setUpstreamAddress(
    uint32_t upstream_index, envoy::config::endpoint::v3::LbEndpoint& endpoint) const {
  auto* socket_address = endpoint.mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
  socket_address->set_port_value(fake_upstreams_[upstream_index]->localAddress()->ip()->port());
}

bool BaseIntegrationTest::getSocketOption(const std::string& listener_name, int level, int optname,
                                          void* optval, socklen_t* optlen, int address_index) {
  bool listeners_ready = false;
  absl::Mutex l;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  test_server_->server().dispatcher().post([&]() {
    listeners = test_server_->server().listenerManager().listeners();
    l.Lock();
    listeners_ready = true;
    l.Unlock();
  });
  l.LockWhen(absl::Condition(&listeners_ready));
  l.Unlock();

  for (auto& listener : listeners) {
    if (listener.get().name() == listener_name) {
      auto& socket_factory = listener.get().listenSocketFactories()[address_index];
      auto socket = socket_factory->getListenSocket(0);
      if (socket->getSocketOption(level, optname, optval, optlen).return_value_ != 0) {
        return false;
      }
      return true;
    }
  }
  return false;
}

void BaseIntegrationTest::registerTestServerPorts(const std::vector<std::string>& port_names,
                                                  IntegrationTestServerPtr& test_server) {
  bool listeners_ready = false;
  absl::Mutex l;
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  test_server->server().dispatcher().post([&listeners, &listeners_ready, &l, &test_server]() {
    listeners = test_server->server().listenerManager().listeners();
    l.Lock();
    listeners_ready = true;
    l.Unlock();
  });
  l.LockWhen(absl::Condition(&listeners_ready));
  l.Unlock();

  auto listener_it = listeners.cbegin();
  auto port_it = port_names.cbegin();
  for (; port_it != port_names.end() && listener_it != listeners.end(); ++listener_it) {
    auto socket_factory_it = listener_it->get().listenSocketFactories().begin();
    for (; socket_factory_it != listener_it->get().listenSocketFactories().end() &&
           port_it != port_names.end();
         ++socket_factory_it, ++port_it) {
      const auto listen_addr = (*socket_factory_it)->localAddress();
      if (listen_addr->type() == Network::Address::Type::Ip) {
        ENVOY_LOG(debug, "registered '{}' as port {}.", *port_it, listen_addr->ip()->port());
        registerPort(*port_it, listen_addr->ip()->port());
      }
    }
  }
  if (test_server->server().admin().has_value()) {
    const auto admin_addr =
        test_server->server().admin()->socket().connectionInfoProvider().localAddress();
    if (admin_addr->type() == Network::Address::Type::Ip) {
      registerPort("admin", admin_addr->ip()->port());
    }
  }
}

std::string getListenerDetails(Envoy::Server::Instance& server) {
  const auto& cbs_maps = server.admin()->getConfigTracker().getCallbacksMap();
  ProtobufTypes::MessagePtr details = cbs_maps.at("listeners")(Matchers::UniversalStringMatcher());
  auto listener_info = dynamic_cast<envoy::admin::v3::ListenersConfigDump&>(*details);
#ifdef ENVOY_ENABLE_YAML
  return MessageUtil::getYamlStringFromMessage(listener_info.dynamic_listeners(0).error_state());
#else
  return listener_info.dynamic_listeners(0).error_state().DebugString();
#endif
}

void BaseIntegrationTest::createGeneratedApiTestServer(
    const std::string& bootstrap_path, const std::vector<std::string>& port_names,
    Server::FieldValidationConfig validator_config, bool allow_lds_rejection) {
  createGeneratedApiTestServer(bootstrap_path, port_names, validator_config, allow_lds_rejection,
                               test_server_);
}

void BaseIntegrationTest::createGeneratedApiTestServer(
    const std::string& bootstrap_path, const std::vector<std::string>& port_names,
    Server::FieldValidationConfig validator_config, bool allow_lds_rejection,
    IntegrationTestServerPtr& test_server) {
  test_server = IntegrationTestServer::create(
      bootstrap_path, version_, on_server_ready_function_, on_server_init_function_,
      deterministic_value_, timeSystem(), *api_, defer_listener_finalization_, process_object_,
      validator_config, concurrency_, drain_time_, drain_strategy_, proxy_buffer_factory_,
      use_real_stats_, use_bootstrap_node_metadata_);
  if (config_helper_.bootstrap().static_resources().listeners_size() > 0 &&
      !defer_listener_finalization_) {

    Event::TestTimeSystem::RealTimeBound bound(listeners_bound_timeout_ms_);
    const char* success = "listener_manager.listener_create_success";
    const char* rejected = "listener_manager.lds.update_rejected";
    for (Stats::CounterSharedPtr success_counter = test_server->counter(success),
                                 rejected_counter = test_server->counter(rejected);
         (success_counter == nullptr ||
          success_counter->value() <
              concurrency_ * config_helper_.bootstrap().static_resources().listeners_size()) &&
         (!allow_lds_rejection || rejected_counter == nullptr || rejected_counter->value() == 0);
         success_counter = test_server->counter(success),
                                 rejected_counter = test_server->counter(rejected)) {
      if (!bound.withinBound()) {
        RELEASE_ASSERT(0, "Timed out waiting for listeners.");
      }
      if (!allow_lds_rejection) {
        RELEASE_ASSERT(rejected_counter == nullptr || rejected_counter->value() == 0,
                       absl::StrCat("Lds update failed. Details\n",
                                    getListenerDetails(test_server->server())));
      }
      // TODO(mattklein123): Switch to events and waitFor().
      time_system_.realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(10));
    }

    registerTestServerPorts(port_names, test_server);
  }
}

void BaseIntegrationTest::createApiTestServer(const ApiFilesystemConfig& api_filesystem_config,
                                              const std::vector<std::string>& port_names,
                                              Server::FieldValidationConfig validator_config,
                                              bool allow_lds_rejection) {
  const std::string eds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.eds_path_, port_map_, version_);
  const std::string cds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.cds_path_, {{"eds_json_path", eds_path}}, port_map_, version_);
  const std::string rds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.rds_path_, port_map_, version_);
  const std::string lds_path = TestEnvironment::temporaryFileSubstitute(
      api_filesystem_config.lds_path_, {{"rds_json_path", rds_path}}, port_map_, version_);
  createGeneratedApiTestServer(TestEnvironment::temporaryFileSubstitute(
                                   api_filesystem_config.bootstrap_path_,
                                   {{"cds_json_path", cds_path}, {"lds_json_path", lds_path}},
                                   port_map_, version_),
                               port_names, validator_config, allow_lds_rejection);
}

void BaseIntegrationTest::sendRawHttpAndWaitForResponse(
    int port, const char* raw_http, std::string* response, bool disconnect_after_headers_complete,
    Network::TransportSocketPtr transport_socket) {
  auto connection = createConnectionDriver(
      port, raw_http,
      [response, disconnect_after_headers_complete](Network::ClientConnection& client,
                                                    const Buffer::Instance& data) -> void {
        response->append(data.toString());
        if (disconnect_after_headers_complete && response->find("\r\n\r\n") != std::string::npos) {
          client.close(Network::ConnectionCloseType::NoFlush);
        }
      },
      std::move(transport_socket));

  if (connection->run() != testing::AssertionSuccess()) {
    FAIL() << "Failed to get expected response within the time bound\n"
           << "received " << *response << "\n";
  }
}

void BaseIntegrationTest::useListenerAccessLog(absl::string_view format) {
  listener_access_log_name_ = TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  ASSERT_TRUE(config_helper_.setListenerAccessLog(listener_access_log_name_, format));
}

std::string BaseIntegrationTest::waitForAccessLog(const std::string& filename, uint32_t entry,
                                                  bool allow_excess_entries,
                                                  Network::ClientConnection* client_connection) {

  // Wait a max of 1s for logs to flush to disk.
  std::string contents;
  const int num_iterations = TSAN_TIMEOUT_FACTOR * 1000;
  for (int i = 0; i < num_iterations; ++i) {
    contents = TestEnvironment::readFileToStringForTest(filename);
    std::vector<std::string> entries = absl::StrSplit(contents, '\n', absl::SkipEmpty());
    if (entries.size() >= entry + 1) {
      // Often test authors will waitForAccessLog() for multiple requests, and
      // not increment the entry number for the second wait. Guard against that.
      EXPECT_TRUE(allow_excess_entries || entries.size() == entry + 1)
          << "Waiting for entry index " << entry << " but it was not the last entry as there were "
          << entries.size() << "\n"
          << contents;
      return entries[entry];
    }
    if (i % 25 == 0 && client_connection != nullptr) {
      // The QUIC default delayed ack timer is 25ms. Wait for any pending ack timers to expire,
      // then run dispatcher to send any pending acks.
      client_connection->dispatcher().run(Envoy::Event::Dispatcher::RunType::NonBlock);
    }
    absl::SleepFor(absl::Milliseconds(1));
  }
  RELEASE_ASSERT(0, absl::StrCat("Timed out waiting for access log. Found: '", contents, "'"));
  return "";
}

void BaseIntegrationTest::createXdsUpstream() {
  if (create_xds_upstream_ == false) {
    return;
  }
  if (tls_xds_upstream_ == false) {
    addFakeUpstream(Http::CodecType::HTTP2);
  } else {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
    auto* tls_cert = common_tls_context->add_tls_certificates();
    tls_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
    tls_cert->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);

    upstream_stats_store_ = std::make_unique<Stats::TestIsolatedStoreImpl>();
    auto context = std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store_->rootScope(),
        std::vector<std::string>{});
    addFakeUpstream(std::move(context), Http::CodecType::HTTP2, /*autonomous_upstream=*/false);
  }
  xds_upstream_ = fake_upstreams_.back().get();
}

void BaseIntegrationTest::createXdsConnection() {
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
}

void BaseIntegrationTest::cleanUpXdsConnection() {
  if (xds_connection_ != nullptr) {
    AssertionResult result = xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    xds_connection_.reset();
  }
}

AssertionResult BaseIntegrationTest::compareDiscoveryRequest(
    const std::string& expected_type_url, const std::string& expected_version,
    const std::vector<std::string>& expected_resource_names,
    const std::vector<std::string>& expected_resource_names_added,
    const std::vector<std::string>& expected_resource_names_removed, bool expect_node,
    const Protobuf::int32 expected_error_code, const std::string& expected_error_substring) {
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw ||
      sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw) {
    return compareSotwDiscoveryRequest(expected_type_url, expected_version, expected_resource_names,
                                       expect_node, expected_error_code, expected_error_substring);
  } else {
    return compareDeltaDiscoveryRequest(expected_type_url, expected_resource_names_added,
                                        expected_resource_names_removed, expected_error_code,
                                        expected_error_substring, expect_node);
  }
}

AssertionResult compareSets(const std::set<std::string>& set1, const std::set<std::string>& set2,
                            absl::string_view name) {
  if (set1 == set2) {
    return AssertionSuccess();
  }
  auto failure = AssertionFailure() << name << " field not as expected.\nExpected: {";
  for (const auto& x : set1) {
    failure << x << ", ";
  }
  failure << "}\nActual: {";
  for (const auto& x : set2) {
    failure << x << ", ";
  }
  return failure << "}";
}

AssertionResult BaseIntegrationTest::compareSotwDiscoveryRequest(
    const std::string& expected_type_url, const std::string& expected_version,
    const std::vector<std::string>& expected_resource_names, bool expect_node,
    const Protobuf::int32 expected_error_code, const std::string& expected_error_substring,
    FakeStream* stream) {
  if (stream == nullptr) {
    stream = xds_stream_.get();
  }

  envoy::service::discovery::v3::DiscoveryRequest discovery_request;
  VERIFY_ASSERTION(stream->waitForGrpcMessage(*dispatcher_, discovery_request));

  if (expect_node) {
    EXPECT_TRUE(discovery_request.has_node());
    EXPECT_FALSE(discovery_request.node().id().empty());
    EXPECT_FALSE(discovery_request.node().cluster().empty());
  } else {
    EXPECT_FALSE(discovery_request.has_node());
  }
  last_node_.CopyFrom(discovery_request.node());

  if (expected_type_url != discovery_request.type_url()) {
    return AssertionFailure() << fmt::format("type_url {} does not match expected {}",
                                             discovery_request.type_url(), expected_type_url);
  }
  if (!(expected_error_code == discovery_request.error_detail().code())) {
    return AssertionFailure() << fmt::format("error_code {} does not match expected {}",
                                             discovery_request.error_detail().code(),
                                             expected_error_code);
  }
  EXPECT_TRUE(
      IsSubstring("", "", expected_error_substring, discovery_request.error_detail().message()));
  const std::set<std::string> resource_names_in_request(discovery_request.resource_names().cbegin(),
                                                        discovery_request.resource_names().cend());
  if (auto resource_name_result = compareSets(
          std::set<std::string>(expected_resource_names.cbegin(), expected_resource_names.cend()),
          resource_names_in_request, "Sotw resource names")) {
    return resource_name_result;
  }
  if (expected_version != discovery_request.version_info()) {
    return AssertionFailure() << fmt::format("version {} does not match expected {} in {}",
                                             discovery_request.version_info(), expected_version,
                                             discovery_request.DebugString());
  }
  return AssertionSuccess();
}

AssertionResult BaseIntegrationTest::waitForPortAvailable(uint32_t port,
                                                          std::chrono::milliseconds timeout) {
  Event::TestTimeSystem::RealTimeBound bound(timeout);
  while (bound.withinBound()) {
    try {
      Network::TcpListenSocket give_me_a_name(
          Network::Utility::getAddressWithPort(
              *Network::Test::getCanonicalLoopbackAddress(version_), port),
          nullptr, true);
      return AssertionSuccess();
    } catch (const EnvoyException&) {
      // The nature of this function requires using a real sleep here.
      timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(100));
    }
  }

  return AssertionFailure() << "Timeout waiting for port availability";
}

envoy::service::discovery::v3::DeltaDiscoveryResponse
BaseIntegrationTest::createExplicitResourcesDeltaDiscoveryResponse(
    const std::string& type_url,
    const std::vector<envoy::service::discovery::v3::Resource>& added_or_updated,
    const std::vector<std::string>& removed) {
  envoy::service::discovery::v3::DeltaDiscoveryResponse response;
  response.set_system_version_info("system_version_info_this_is_a_test");
  response.set_type_url(type_url);
  *response.mutable_resources() = {added_or_updated.begin(), added_or_updated.end()};
  *response.mutable_removed_resources() = {removed.begin(), removed.end()};
  static int next_nonce_counter = 0;
  response.set_nonce(absl::StrCat("nonce", next_nonce_counter++));
  return response;
}

AssertionResult BaseIntegrationTest::compareDeltaDiscoveryRequest(
    const std::string& expected_type_url,
    const std::vector<std::string>& expected_resource_subscriptions,
    const std::vector<std::string>& expected_resource_unsubscriptions, FakeStreamPtr& xds_stream,
    const Protobuf::int32 expected_error_code, const std::string& expected_error_substring,
    bool expect_node) {
  envoy::service::discovery::v3::DeltaDiscoveryRequest request;
  VERIFY_ASSERTION(xds_stream->waitForGrpcMessage(*dispatcher_, request));

  // Verify all we care about node.
  if (expect_node &&
      (!request.has_node() || request.node().id().empty() || request.node().cluster().empty())) {
    return AssertionFailure() << "Weird node field";
  }
  last_node_.CopyFrom(request.node());
  if (request.type_url() != expected_type_url) {
    return AssertionFailure() << fmt::format("type_url {} does not match expected {}.",
                                             request.type_url(), expected_type_url);
  }
  // Sort to ignore ordering.
  std::set<std::string> expected_sub{expected_resource_subscriptions.begin(),
                                     expected_resource_subscriptions.end()};
  std::set<std::string> expected_unsub{expected_resource_unsubscriptions.begin(),
                                       expected_resource_unsubscriptions.end()};
  std::set<std::string> actual_sub{request.resource_names_subscribe().begin(),
                                   request.resource_names_subscribe().end()};
  std::set<std::string> actual_unsub{request.resource_names_unsubscribe().begin(),
                                     request.resource_names_unsubscribe().end()};
  auto sub_result = compareSets(expected_sub, actual_sub, "expected_resource_subscriptions");
  if (!sub_result) {
    return sub_result;
  }
  auto unsub_result =
      compareSets(expected_unsub, actual_unsub, "expected_resource_unsubscriptions");
  if (!unsub_result) {
    return unsub_result;
  }
  // (We don't care about response_nonce or initial_resource_versions.)

  if (request.error_detail().code() != expected_error_code) {
    return AssertionFailure() << fmt::format(
               "error code {} does not match expected {}. (Error message is {}).",
               request.error_detail().code(), expected_error_code,
               request.error_detail().message());
  }
  if (expected_error_code != Grpc::Status::WellKnownGrpcStatus::Ok &&
      request.error_detail().message().find(expected_error_substring) == std::string::npos) {
    return AssertionFailure() << "\"" << expected_error_substring
                              << "\" is not a substring of actual error message \""
                              << request.error_detail().message() << "\"";
  }
  return AssertionSuccess();
}

// Attempt to heuristically discover missing tag-extraction rules when new stats are added.
// This is done by looking through the entire config for fields named `stat_prefix`, and then
// validating that those values do not appear in the tag-extracted name of any stat. The alternate
// approach of looking for the prefix in the extracted tags was more difficult because in the
// tests some prefix values are reused (leading to false negatives) and some tests have base
// configuration that sets a stat_prefix but don't produce any stats at all with that
// configuration (leading to false positives).
//
// To add a rule, see `source/common/config/well_known_names.cc`.
//
// This is done in all integration tests because it is testing new stats and scopes that are
// created for which the author isn't aware that tag extraction rules need to be written, and thus
// the author wouldn't think to write tests for that themselves.
void BaseIntegrationTest::checkForMissingTagExtractionRules() {
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      test_server_->adminAddress(), "GET", "/config_dump", "", Http::CodecType::HTTP1);
  EXPECT_TRUE(response->complete());
  if (!response->complete()) {
    // Allow the rest of the test to complete for better diagnostic information about the failure.
    return;
  }

  EXPECT_EQ("200", response->headers().getStatusValue());
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());

  std::vector<std::string> stat_prefixes;
  Json::ObjectCallback find_stat_prefix = [&](const std::string& name,
                                              const Json::Object& root) -> bool {
    // Looking for `stat_prefix` is based on precedent for how this is usually named in the
    // config. If there are other names used for a similar purpose, this check could be expanded
    // to add them also.
    if (name == "stat_prefix") {
      auto prefix = root.asString();
      if (!prefix.empty()) {
        stat_prefixes.push_back(prefix);
      }
    } else if (root.isObject()) {
      root.iterate(find_stat_prefix);
    } else if (root.isArray()) {
      std::vector<Json::ObjectSharedPtr> elements = root.asObjectArray();
      for (const auto& element : elements) {
        find_stat_prefix("", *element);
      }
    }
    return true;
  };
  find_stat_prefix("", *json);
  ENVOY_LOG_MISC(debug, "discovered stat_prefixes {}", stat_prefixes);

  auto check_metric = [&](auto& metric) {
    // Validate that the `stat_prefix` string doesn't appear in the tag-extracted name, indicating
    // that it wasn't extracted.
    const std::string tag_extracted_name = metric.tagExtractedName();
    for (const std::string& stat_prefix : stat_prefixes) {
      EXPECT_EQ(tag_extracted_name.find(stat_prefix), std::string::npos)
          << "Missing stat tag-extraction rule for stat '" << tag_extracted_name
          << "' and stat_prefix '" << stat_prefix << "'";
    }
  };
  test_server_->statStore().forEachCounter(nullptr, check_metric);
  test_server_->statStore().forEachGauge(nullptr, check_metric);
  test_server_->statStore().forEachHistogram(nullptr, check_metric);
}
} // namespace Envoy
