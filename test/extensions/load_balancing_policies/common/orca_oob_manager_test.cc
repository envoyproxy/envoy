#include <chrono>
#include <memory>
#include <sstream>

#include "envoy/extensions/load_balancing_policies/common/v3/common.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "source/common/network/utility.h"
#include "source/extensions/load_balancing_policies/common/orca_oob_manager.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "test/common/http/common.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Common {
namespace {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

// MOCK_METHOD returns a raw Http::CodecClient*; createCodecClient wraps in unique_ptr to
// transfer ownership to OobSession.
class TestOrcaOobManager : public OrcaOobManager {
public:
  using OrcaOobManager::OrcaOobManager;
  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override {
    return Http::CodecClientPtr(createCodecClient_(data));
  }
  MOCK_METHOD(Http::CodecClient*, createCodecClient_,
              (Upstream::Host::CreateConnectionData & data));
};

// MockHost that captures createOrcaReportingConnection args; MockHostLight
// otherwise discards them.
class CapturingMockHost : public NiceMock<Upstream::MockHost> {
public:
  Upstream::Host::CreateConnectionData createOrcaReportingConnection(
      Event::Dispatcher& dispatcher,
      Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
      Network::UpstreamTransportSocketFactory& factory,
      Network::Address::InstanceConstSharedPtr orca_address) const override {
    last_transport_socket_options_ = transport_socket_options;
    last_orca_address_ = orca_address;
    last_factory_ = &factory;
    return Upstream::MockHostLight::createOrcaReportingConnection(
        dispatcher, transport_socket_options, factory, orca_address);
  }

  mutable Network::TransportSocketOptionsConstSharedPtr last_transport_socket_options_;
  mutable Network::Address::InstanceConstSharedPtr last_orca_address_;
  mutable const Network::UpstreamTransportSocketFactory* last_factory_{nullptr};
};

class OrcaOobManagerLifecycleTest : public testing::Test, public Event::TestUsingSimulatedTime {
protected:
  void SetUp() override {
    OrcaWeightManagerConfig weight_config{};
    weight_config.weight_update_period = std::chrono::seconds(1);
    weight_config.weight_expiration_period = std::chrono::seconds(180);
    weight_config.blackout_period = std::chrono::seconds(10);
    report_handler_ = std::make_shared<OrcaLoadReportHandler>(weight_config, simTime());
  }

  std::unique_ptr<TestOrcaOobManager> makeManager() {
    OrcaOobManagerConfig config;
    config.reporting_period = std::chrono::seconds(10);
    return makeManager(config);
  }

  std::unique_ptr<TestOrcaOobManager> makeManager(const OrcaOobManagerConfig& config) {
    return std::make_unique<TestOrcaOobManager>(config, priority_set_, dispatcher_, random_,
                                                *stats_store_.rootScope(), report_handler_);
  }

  uint64_t activeOobSessions() {
    return stats_store_.gauge("lb_orca_oob.active_sessions", Stats::Gauge::ImportMode::Accumulate)
        .value();
  }

  uint64_t oobCounter(absl::string_view name) {
    return stats_store_.counter(absl::StrCat("lb_orca_oob.", name)).value();
  }

  Upstream::HostSharedPtr makeHost() {
    auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
    ON_CALL(*host, address())
        .WillByDefault(Return(*Network::Utility::resolveUrl("tcp://10.0.0.1:80")));
    return host;
  }

  // OobSession ctor calls createTimer twice (attempt, then inactivity); MockTimer EXPECT_CALLs
  // match LIFO, so we push the inactivity mock first and return the attempt mock.
  NiceMock<Event::MockTimer>* installAttemptTimer() {
    new NiceMock<Event::MockTimer>(&dispatcher_);        // inactivity (consumed second)
    return new NiceMock<Event::MockTimer>(&dispatcher_); // attempt (consumed first)
  }

  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore stats_store_;
  OrcaLoadReportHandlerSharedPtr report_handler_;
};

TEST_F(OrcaOobManagerLifecycleTest, HostAddedSchedulesSession) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});
  EXPECT_EQ(activeOobSessions(), 1);

  // Re-adding the same host pointer hits try_emplace's already-present branch.
  priority_set_.runUpdateCallbacks(0, {host}, {});
  EXPECT_EQ(activeOobSessions(), 1);

  EXPECT_CALL(*manager, createCodecClient_(_)).WillOnce(Return(nullptr));
  attempt_timer->invokeCallback();
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerLifecycleTest, HostRemovedDisarmsAndDecrementsGauge) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  installAttemptTimer();
  auto host = makeHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});
  EXPECT_EQ(activeOobSessions(), 1);

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  priority_set_.runUpdateCallbacks(0, {}, {host});
  EXPECT_EQ(activeOobSessions(), 0);

  // Removing a host the manager never tracked exercises the find()==end() branch.
  priority_set_.runUpdateCallbacks(0, {}, {makeHost()});
  EXPECT_EQ(activeOobSessions(), 0);
}

TEST_F(OrcaOobManagerLifecycleTest, NonPositivePeriodClampedToDefault) {
  OrcaOobManagerConfig config;
  config.reporting_period = std::chrono::milliseconds(0);
  auto manager = makeManager(config);
  ASSERT_OK(manager->initialize());

  // onHostsAdded jitters by random % period; random = default-1 makes the initial delay
  // default-1 only if the period clamped to the default (and a zero period would SIGFPE).
  ON_CALL(random_, random()).WillByDefault(Return(kDefaultOobReportingPeriodMs - 1));
  auto* attempt_timer = installAttemptTimer();
  EXPECT_CALL(*attempt_timer,
              enableTimer(std::chrono::milliseconds(kDefaultOobReportingPeriodMs - 1), _));
  priority_set_.runUpdateCallbacks(0, {makeHost()}, {});
  EXPECT_EQ(activeOobSessions(), 1);

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerLifecycleTest, DestructionDisarmsActiveSessions) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  installAttemptTimer();
  priority_set_.runUpdateCallbacks(0, {makeHost()}, {});
  EXPECT_EQ(activeOobSessions(), 1);

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// Wire fixture: drives end-to-end ORCA OOB decode path through a real CodecClient
// (CodecClientForTest) layered over Network::MockClientConnection +
// Http::MockClientConnection. Models the gRPC health checker's wire-test pattern
// (test/common/upstream/health_checker_impl_test.cc).
class OrcaOobManagerWireTest : public OrcaOobManagerLifecycleTest {
protected:
  struct OobAttempt {
    NiceMock<Network::MockClientConnection>* network_connection{nullptr};
    NiceMock<Http::MockClientConnection>* codec{nullptr};
    // Owned: MockRequestEncoder (which owns its MockStream) outlives the OobSession's
    // raw pointer to it because a self-initiated close in the test does not actually
    // tear the request_encoder down.
    std::unique_ptr<NiceMock<Http::MockRequestEncoder>> request_encoder;
    Http::ResponseDecoder* response_decoder{nullptr};
    CodecClientForTest* codec_client{nullptr};
  };

  // Build the per-attempt mocks. OobSession takes ownership of network_connection (via
  // CodecClientForTest) and codec (via codec_->reset). request_encoder lifetime is owned by
  // the fixture; MockClientConnection::newStream returns a ref into it. close() is stubbed
  // to a no-op so wire tests can assert stream_failures from explicit raise* hooks; see
  // SyncLocalCloseDuringTearDownDoesNotDoubleCount for the synchronous-close variant.
  // Non-null capture pointers receive :authority/:scheme from encodeHeaders.
  std::unique_ptr<OobAttempt> makeAttempt(std::string* captured_authority = nullptr,
                                          std::string* captured_scheme = nullptr) {
    auto attempt = std::make_unique<OobAttempt>();
    attempt->network_connection = new NiceMock<Network::MockClientConnection>();
    attempt->codec = new NiceMock<Http::MockClientConnection>();
    attempt->request_encoder = std::make_unique<NiceMock<Http::MockRequestEncoder>>();
    EXPECT_CALL(*attempt->codec, newStream(_))
        .WillOnce(testing::DoAll(SaveArgAddress(&attempt->response_decoder),
                                 testing::ReturnRef(*attempt->request_encoder)));
    EXPECT_CALL(*attempt->request_encoder, encodeHeaders(_, false))
        .WillOnce(testing::Invoke([captured_authority, captured_scheme](
                                      const Http::RequestHeaderMap& h, bool) -> absl::Status {
          if (captured_authority != nullptr) {
            *captured_authority = std::string(h.getHostValue());
          }
          if (captured_scheme != nullptr) {
            *captured_scheme = std::string(h.getSchemeValue());
          }
          return absl::OkStatus();
        }));
    EXPECT_CALL(*attempt->request_encoder, encodeData(_, true));
    ON_CALL(*attempt->network_connection, close(_, _)).WillByDefault(testing::Return());
    ON_CALL(*attempt->network_connection, close(_)).WillByDefault(testing::Return());
    return attempt;
  }

  // Wire host->createOrcaReportingConnection (which delegates to MockHostLight::
  // createConnection_) to hand back the network_connection raw ptr we control.
  void wireConnectionFor(Upstream::HostSharedPtr host, OobAttempt& attempt) {
    using Upstream::MockHostLight;
    host_description_for_codec_ = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    EXPECT_CALL(static_cast<NiceMock<Upstream::MockHost>&>(*host), createConnection_(_, _))
        .WillOnce(testing::InvokeWithoutArgs([this, &attempt]() {
          MockHostLight::MockCreateConnectionData data;
          data.connection_ = attempt.network_connection;
          data.host_description_ = host_description_for_codec_;
          return data;
        }));
  }

  Http::CodecClient* attachCodecClient(OobAttempt& attempt,
                                       Upstream::Host::CreateConnectionData& data) {
    attempt.codec_client =
        new CodecClientForTest(Http::CodecType::HTTP2, std::move(data.connection_), attempt.codec,
                               /*destroy_cb=*/nullptr, data.host_description_, dispatcher_);
    return attempt.codec_client;
  }

  // Capture-by-ref the EXPECT_CALL+Invoke pattern: when the OobSession fires the test seam
  // createCodecClient_, hand back a CodecClientForTest layered over the supplied attempt.
  void expectCreateCodecClient(TestOrcaOobManager& manager, OobAttempt& attempt) {
    EXPECT_CALL(manager, createCodecClient_(_))
        .WillOnce(testing::Invoke([this, &attempt](Upstream::Host::CreateConnectionData& data) {
          return attachCodecClient(attempt, data);
        }));
  }

  void respondHeadersOk(OobAttempt& attempt) {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/grpc"}};
    attempt.response_decoder->decodeHeaders(
        std::make_unique<Http::TestResponseHeaderMapImpl>(headers), false);
  }

  void respondReport(OobAttempt& attempt, const xds::data::orca::v3::OrcaLoadReport& report) {
    auto buffer = Grpc::Common::serializeToGrpcFrame(report);
    attempt.response_decoder->decodeData(*buffer, false);
  }

  void respondTrailers(OobAttempt& attempt, Grpc::Status::GrpcStatus status) {
    auto trailers = std::make_unique<Http::TestResponseTrailerMapImpl>();
    trailers->setGrpcStatus(static_cast<int>(status));
    attempt.response_decoder->decodeTrailers(std::move(trailers));
  }

  void wireAddressHostname(Upstream::MockHost& host, absl::string_view address_url) {
    auto address = *Network::Utility::resolveUrl(std::string(address_url));
    addresses_.push_back(address);
    ON_CALL(host, address()).WillByDefault(testing::Return(address));
    ON_CALL(host, hostname()).WillByDefault(testing::ReturnRef(empty_hostname_));
    // The manager resolves the factory when match metadata is set.
    ON_CALL(host, resolveTransportSocketFactory(_, _, _))
        .WillByDefault(testing::ReturnRef(*host.socket_factory_));
  }

  // Returns a MockHost with address+hostname+canCreateConnection wired up enough for
  // OobSession::connectAndStream to traverse without segfault.
  std::shared_ptr<NiceMock<Upstream::MockHost>> makeWiredHost() {
    auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
    wireAddressHostname(*host, "tcp://10.0.0.1:80");
    return host;
  }

  // CapturingMockHost wired to hand back attempt.network_connection on createConnection_.
  std::shared_ptr<CapturingMockHost> makeWiredCapturingHost(absl::string_view address_url,
                                                            TestOrcaOobManager& manager,
                                                            OobAttempt& attempt) {
    auto host = std::make_shared<CapturingMockHost>();
    wireAddressHostname(*host, address_url);
    wireConnectionFor(host, attempt);
    expectCreateCodecClient(manager, attempt);
    return host;
  }

  std::string empty_hostname_;
  std::vector<Network::Address::InstanceConstSharedPtr> addresses_;
  Upstream::HostDescriptionConstSharedPtr host_description_for_codec_;
};

TEST_F(OrcaOobManagerWireTest, ReportReceivedUpdatesHostWeight) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  // Attach OrcaHostLbPolicyData (in production, OrcaWeightManager does this; here we
  // attach directly so the wire test does not depend on in-band manager presence).
  host->addLbPolicyData(std::make_unique<OrcaHostLbPolicyData>(report_handler_));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  xds::data::orca::v3::OrcaLoadReport empty_report;
  respondReport(*attempt, empty_report);
  EXPECT_EQ(oobCounter("reports_received"), 0);
  EXPECT_EQ(oobCounter("stream_failures"), 0);

  xds::data::orca::v3::OrcaLoadReport report;
  report.set_application_utilization(0.5);
  report.set_rps_fractional(1000);
  respondReport(*attempt, report);
  EXPECT_EQ(oobCounter("reports_received"), 1);
  auto data_opt = host->typedLbPolicyData<OrcaHostLbPolicyData>();
  ASSERT_TRUE(data_opt.has_value());
  // Proves report flowed through; exact formula tested in OrcaWeightManager tests.
  EXPECT_GT(data_opt->weight_.load(), 1u);

  xds::data::orca::v3::OrcaLoadReport invalid_report;
  invalid_report.set_application_utilization(0.5);
  respondReport(*attempt, invalid_report);
  EXPECT_EQ(oobCounter("reports_received"), 2);
  EXPECT_EQ(oobCounter("report_errors"), 1);

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerWireTest, CodecNoopCallbacksAreSafe) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();

  attempt->response_decoder->decodeMetadata(std::make_unique<Http::MetadataMap>());
  attempt->response_decoder->decode1xxHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>());
  std::ostringstream dump;
  attempt->response_decoder->dumpState(dump, 0);

  attempt->request_encoder->stream_.runHighWatermarkCallbacks();
  attempt->request_encoder->stream_.runLowWatermarkCallbacks();
  attempt->network_connection->runHighWatermarkCallbacks();
  attempt->network_connection->runLowWatermarkCallbacks();

  EXPECT_EQ(oobCounter("stream_failures"), 0);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerWireTest, EndStreamDataWithoutTrailersIsTransientFailure) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  Buffer::OwnedImpl empty_data;
  attempt->response_decoder->decodeData(empty_data, true);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, NullDialAddressTriggersEnvoyBug) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host, orcaReportingAddress()).WillByDefault(Return(nullptr));
  ON_CALL(*host, address()).WillByDefault(Return(nullptr));
  ON_CALL(*host, hostname()).WillByDefault(testing::ReturnRef(empty_hostname_));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  // Assertions sit inside EXPECT_ENVOY_BUG so they run in-process on the release path; in debug
  // the abort fires first and skips them.
  EXPECT_CALL(*manager, createCodecClient_(_)).Times(0);
  EXPECT_ENVOY_BUG(
      {
        attempt_timer->invokeCallback();
        EXPECT_EQ(oobCounter("stream_failures"), 1);
        EXPECT_TRUE(attempt_timer->enabled());
      },
      "ORCA OOB host has no resolvable address");
}

TEST_F(OrcaOobManagerWireTest, UnimplementedTrailerIsTerminal) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  respondTrailers(*attempt, Grpc::Status::WellKnownGrpcStatus::Unimplemented);
  EXPECT_EQ(oobCounter("stream_terminated"), 1);
  EXPECT_EQ(activeOobSessions(), 0);
}

TEST_F(OrcaOobManagerWireTest, GoAwayNoErrorDefersUntilNextDecode) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  host->addLbPolicyData(std::make_unique<OrcaHostLbPolicyData>(report_handler_));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  attempt->codec_client->raiseGoAway(Http::GoAwayErrorCode::NoError);
  EXPECT_EQ(oobCounter("stream_failures"), 0);

  // Subsequent report is delivered, then deferred GOAWAY tears the session down.
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_application_utilization(0.5);
  report.set_rps_fractional(1000);
  respondReport(*attempt, report);
  EXPECT_EQ(oobCounter("reports_received"), 1);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, GoAwayOtherIsImmediateTransient) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  attempt->codec_client->raiseGoAway(Http::GoAwayErrorCode::Other);
  respondTrailers(*attempt, Grpc::Status::WellKnownGrpcStatus::Unimplemented);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
  EXPECT_EQ(oobCounter("stream_terminated"), 0);
}

TEST_F(OrcaOobManagerWireTest, ReportWithoutLbPolicyDataIncrementsReportErrors) {
  // Host has no OrcaHostLbPolicyData attached (would be done by OrcaWeightManager
  // in production; this test simulates the init-order race the architecture
  // documents as v1-acceptable). onReport increments report_errors and bails.
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_application_utilization(0.5);
  report.set_rps_fractional(1000);
  respondReport(*attempt, report);
  EXPECT_EQ(oobCounter("report_errors"), 1);
  EXPECT_EQ(oobCounter("reports_received"), 1); // counter still bumps before the data check

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerWireTest, NonGrpcResponseTransientFailure) {
  // Server returns HTTP 500 with non-grpc content-type (e.g., the request was routed
  // to a non-gRPC handler). decodeHeaders' isGrpcResponseHeaders branch should
  // route through onRpcComplete -> handleTransientFailure.
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  // Non-grpc 500 response, end_stream=true (server bailed cleanly).
  Http::TestResponseHeaderMapImpl headers{{":status", "500"}, {"content-type", "text/plain"}};
  attempt->response_decoder->decodeHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(headers), true);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

// Regression test for the tearDownCodec invariant: production ConnectionImpl::close(Abort)
// raises LocalClose synchronously. OobSession::tearDownCodec must null codec_client_
// BEFORE calling close() so the re-entry into onConnectionEvent short-circuits. Without
// the move-before-close ordering, this test would record stream_failures==2 (one from
// the GoAway path, one from the sync onConnectionEvent re-entry into handleTransientFailure).
TEST_F(OrcaOobManagerWireTest, NonGrpcResponseEndStreamFalseTransient) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  // Non-grpc 503 with end_stream=false drives onRpcComplete's resetStream branch.
  Http::TestResponseHeaderMapImpl headers{{":status", "503"}, {"content-type", "text/plain"}};
  attempt->response_decoder->decodeHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(headers), false);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, TrailersOnlyResponseTreatedAsTerminal) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  // 200 OK + grpc-status + end_stream=true is the trailers-only frame; covers the
  // end_stream branch in decodeHeaders that bypasses decodeTrailers entirely.
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"},
      {"content-type", "application/grpc"},
      {"grpc-status",
       absl::StrCat(static_cast<int>(Grpc::Status::WellKnownGrpcStatus::Unimplemented))}};
  attempt->response_decoder->decodeHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(headers), true);
  EXPECT_EQ(oobCounter("stream_terminated"), 1);
  EXPECT_EQ(activeOobSessions(), 0);
}

TEST_F(OrcaOobManagerWireTest, MalformedGrpcFrameTriggersTransientFailure) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  Buffer::OwnedImpl bad(std::string("\x02\x00\x00\x00\x00", 5));
  attempt->response_decoder->decodeData(bad, false);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, CompressedFrameRejectedAsTransient) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  // Frame decodes cleanly; OobSession rejects compressed frames.
  Buffer::OwnedImpl bad(std::string("\x01\x00\x00\x00\x00", 5));
  attempt->response_decoder->decodeData(bad, false);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, InvalidProtoPayloadTriggersTransientFailure) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  // Valid frame envelope (flag=0, length=1) wrapping a single-byte payload that is an
  // unterminated varint, so OrcaLoadReport::ParseFromZeroCopyStream returns false.
  Buffer::OwnedImpl bad(std::string("\x00\x00\x00\x00\x01\x80", 6));
  attempt->response_decoder->decodeData(bad, false);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, EncodeHeadersFailureTriggersTransientFailure) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  // Custom attempt: encodeHeaders returns NOT_OK so encodeData is never called.
  auto attempt = std::make_unique<OobAttempt>();
  attempt->network_connection = new NiceMock<Network::MockClientConnection>();
  attempt->codec = new NiceMock<Http::MockClientConnection>();
  attempt->request_encoder = std::make_unique<NiceMock<Http::MockRequestEncoder>>();
  EXPECT_CALL(*attempt->codec, newStream(_))
      .WillOnce(testing::DoAll(SaveArgAddress(&attempt->response_decoder),
                               testing::ReturnRef(*attempt->request_encoder)));
  EXPECT_CALL(*attempt->request_encoder, encodeHeaders(_, false))
      .WillOnce(testing::Return(absl::InternalError("encode bust")));
  ON_CALL(*attempt->network_connection, close(_, _)).WillByDefault(testing::Return());
  ON_CALL(*attempt->network_connection, close(_)).WillByDefault(testing::Return());

  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, RemoteCloseTriggersTransientFailure) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  // Run OobSession's connection callback before CodecClient's callback so onConnectionEvent sees
  // codec_client_ live; CodecClient then resets the stream after OobSession has torn it down.
  ASSERT_GE(attempt->network_connection->callbacks_.size(), 2);
  auto* oob_callback = attempt->network_connection->callbacks_.back();
  attempt->network_connection->callbacks_.pop_back();
  attempt->network_connection->callbacks_.push_front(oob_callback);
  attempt->network_connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

TEST_F(OrcaOobManagerWireTest, HostnameUsedAsAuthority) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  std::string hostname = "myorca.example";
  ON_CALL(*host, hostname()).WillByDefault(testing::ReturnRef(hostname));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  std::string captured_authority;
  auto attempt = makeAttempt(&captured_authority);
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();
  EXPECT_EQ(captured_authority, "myorca.example");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerWireTest, PipeHostFallsBackToClusterName) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  wireAddressHostname(*host, "unix:///tmp/orca.sock");
  priority_set_.runUpdateCallbacks(0, {host}, {});

  std::string captured_authority;
  auto attempt = makeAttempt(&captured_authority);
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();
  // Pipe address has no ip(); authority falls through to the cluster name.
  EXPECT_EQ(captured_authority, "fake_cluster");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST_F(OrcaOobManagerWireTest, SyncLocalCloseDuringTearDownDoesNotDoubleCount) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  auto attempt = makeAttempt();
  // Override the fixture's no-op close() stubs so close() raises sync LocalClose, matching
  // production ConnectionImpl::close(Abort) semantics.
  auto* raw_conn = attempt->network_connection;
  ON_CALL(*raw_conn, close(_, _)).WillByDefault(testing::InvokeWithoutArgs([raw_conn]() {
    raw_conn->raiseEvent(Network::ConnectionEvent::LocalClose);
  }));
  ON_CALL(*raw_conn, close(_)).WillByDefault(testing::InvokeWithoutArgs([raw_conn]() {
    raw_conn->raiseEvent(Network::ConnectionEvent::LocalClose);
  }));
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  attempt_timer->invokeCallback();

  respondHeadersOk(*attempt);
  // GoAway(Other) -> handleTransientFailure -> tearDownCodec -> sync close -> sync
  // onConnectionEvent(LocalClose) re-entry. With the fix, the re-entry sees a null
  // codec_client_ and returns; stream_failures stays at 1.
  attempt->codec_client->raiseGoAway(Http::GoAwayErrorCode::Other);
  EXPECT_EQ(oobCounter("stream_failures"), 1);
}

// Every OOB attempt forces ALPN "h2".
TEST_F(OrcaOobManagerWireTest, AlpnForcedToH2OnEveryOobConnection) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto attempt = makeAttempt();
  auto host = makeWiredCapturingHost("tcp://10.0.0.2:80", *manager, *attempt);
  priority_set_.runUpdateCallbacks(0, {host}, {});

  attempt_timer->invokeCallback();

  // Captured transport_socket_options must have ALPN override {"h2"}.
  ASSERT_NE(host->last_transport_socket_options_, nullptr);
  EXPECT_THAT(host->last_transport_socket_options_->applicationProtocolListOverride(),
              testing::ElementsAre("h2"));

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// port_value applies to orcaReportingAddress(), not address().
TEST_F(OrcaOobManagerWireTest, PortOverrideAppliedToOrcaReportingAddress) {
  OrcaOobManagerConfig config;
  config.reporting_period = std::chrono::seconds(10);
  config.port_value = 9001;
  auto manager = makeManager(config);
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  std::string captured_authority;
  auto attempt = makeAttempt(&captured_authority);
  auto host = makeWiredCapturingHost("tcp://1.1.1.1:80", *manager, *attempt);
  // orcaReportingAddress() deliberately differs from address().
  auto orca_address = *Network::Utility::resolveUrl("tcp://2.2.2.2:80");
  addresses_.push_back(orca_address);
  ON_CALL(*host, orcaReportingAddress()).WillByDefault(Return(orca_address));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  attempt_timer->invokeCallback();

  ASSERT_NE(host->last_orca_address_, nullptr);
  EXPECT_EQ(host->last_orca_address_->asString(), "2.2.2.2:9001");
  EXPECT_EQ(captured_authority, "2.2.2.2:9001");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// transport_socket_match_criteria resolves the factory once and passes it to the connection.
TEST_F(OrcaOobManagerWireTest, TransportSocketMatchCriteriaResolvesFactoryOnce) {
  envoy::extensions::load_balancing_policies::common::v3::OrcaOobReportingConfig proto;
  (*proto.mutable_transport_socket_match_criteria()->mutable_fields())["useMTLS"].set_bool_value(
      true);
  OrcaOobManagerConfig config;
  applyOrcaOobConnectionOverrides(proto, config);
  auto manager = makeManager(config);
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto attempt = makeAttempt();
  auto host = makeWiredCapturingHost("tcp://10.0.0.3:80", *manager, *attempt);
  EXPECT_CALL(*host, resolveTransportSocketFactory(
                         _, testing::Truly([](const envoy::config::core::v3::Metadata* m) {
                           return m != nullptr &&
                                  m->filter_metadata().contains("envoy.transport_socket_match");
                         }),
                         _))
      .WillOnce(testing::ReturnRef(*host->socket_factory_));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  attempt_timer->invokeCallback();

  EXPECT_EQ(host->last_factory_, host->socket_factory_.get());

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// authority overrides the :authority header.
TEST_F(OrcaOobManagerWireTest, AuthorityOverrideUsedInRequestHeaders) {
  envoy::extensions::load_balancing_policies::common::v3::OrcaOobReportingConfig proto;
  proto.set_authority("orca.example.com");
  OrcaOobManagerConfig config;
  applyOrcaOobConnectionOverrides(proto, config);
  auto manager = makeManager(config);
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  std::string captured_authority;
  auto attempt = makeAttempt(&captured_authority);
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();
  EXPECT_EQ(captured_authority, "orca.example.com");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// :scheme is "http" when the transport socket factory is not secure.
TEST_F(OrcaOobManagerWireTest, SchemeIsHttpForPlaintextConnection) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  priority_set_.runUpdateCallbacks(0, {host}, {});

  std::string captured_authority;
  std::string captured_scheme;
  auto attempt = makeAttempt(&captured_authority, &captured_scheme);
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();
  EXPECT_EQ(captured_scheme, "http");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

// :scheme is "https" when the transport socket factory implements secure
// transport (covers secure non-SSL transports like ALTS, unlike ssl() sniffing).
TEST_F(OrcaOobManagerWireTest, SchemeIsHttpsForSecureTransport) {
  auto manager = makeManager();
  ASSERT_OK(manager->initialize());

  auto* attempt_timer = installAttemptTimer();
  auto host = makeWiredHost();
  ON_CALL(static_cast<Network::MockTransportSocketFactory&>(*host->socket_factory_),
          implementsSecureTransport())
      .WillByDefault(Return(true));
  priority_set_.runUpdateCallbacks(0, {host}, {});

  std::string captured_authority;
  std::string captured_scheme;
  auto attempt = makeAttempt(&captured_authority, &captured_scheme);
  wireConnectionFor(host, *attempt);
  expectCreateCodecClient(*manager, *attempt);
  attempt_timer->invokeCallback();
  EXPECT_EQ(captured_scheme, "https");

  EXPECT_CALL(dispatcher_, deferredDelete_(_)).Times(AtLeast(1));
  manager.reset();
}

TEST(ApplyOrcaOobConnectionOverridesTest, EmptyProtoLeavesDefaults) {
  envoy::extensions::load_balancing_policies::common::v3::OrcaOobReportingConfig proto;
  OrcaOobManagerConfig config;
  applyOrcaOobConnectionOverrides(proto, config);
  EXPECT_EQ(config.port_value, 0u);
  EXPECT_TRUE(config.authority.empty());
  EXPECT_EQ(config.transport_socket_match_metadata, nullptr);
}

TEST(ApplyOrcaOobConnectionOverridesTest, PopulatedProtoIsMerged) {
  envoy::extensions::load_balancing_policies::common::v3::OrcaOobReportingConfig proto;
  proto.set_port_value(9001);
  proto.set_authority("backend.example.com");
  (*proto.mutable_transport_socket_match_criteria()->mutable_fields())["useMTLS"].set_bool_value(
      true);

  OrcaOobManagerConfig config;
  applyOrcaOobConnectionOverrides(proto, config);
  EXPECT_EQ(config.port_value, 9001u);
  EXPECT_EQ(config.authority, "backend.example.com");
  ASSERT_NE(config.transport_socket_match_metadata, nullptr);
  EXPECT_TRUE(config.transport_socket_match_metadata->filter_metadata().contains(
      "envoy.transport_socket_match"));
  EXPECT_TRUE(config.transport_socket_match_metadata->filter_metadata()
                  .at("envoy.transport_socket_match")
                  .fields()
                  .at("useMTLS")
                  .bool_value());
}

} // namespace
} // namespace Common
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
