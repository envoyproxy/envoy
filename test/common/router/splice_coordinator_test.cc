// Unit tests for Router::SpliceCoordinator. The fixture injects a fake pump so tests can drive arm,
// engage, complete, finalize, and reset without kernel I/O. The kernel splice path is covered by
// splice_pump_test.cc.

#include "envoy/network/address.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/splice_coordinator.h"
#include "source/common/router/upstream_request.h"
#include "source/common/tcp_proxy/splice_pump.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

// Non-invalid fds accepted by spliceableFd.
constexpr os_fd_t kUpstreamFd = 11;
constexpr os_fd_t kDownstreamFd = 10;
// Content-Length above MinSpliceBodyBytes.
constexpr uint64_t kBodyBytes = 128 * 1024;

// No-I/O SplicePump test double.
class FakeSplicePump : public TcpProxy::SplicePump {
public:
  FakeSplicePump(os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls, Event::Dispatcher& dispatcher,
                 TcpProxy::SplicePump::CompletionCb on_complete,
                 TcpProxy::SplicePump::BytesCb on_u2d_bytes,
                 TcpProxy::SplicePump::BytesCb on_d2u_bytes)
      : TcpProxy::SplicePump(down_fd, up_fd, up_is_ktls, dispatcher, std::move(on_complete),
                             std::move(on_u2d_bytes), std::move(on_d2u_bytes)) {}

  bool createPipes(bool need_u2d, bool need_d2u) override {
    create_pipes_called_ = true;
    need_u2d_ = need_u2d;
    need_d2u_ = need_d2u;
    return create_pipes_result_;
  }
  bool prepare(std::string initial_u2d, std::string initial_d2u) override {
    prepare_called_ = true;
    prepare_initial_u2d_ = std::move(initial_u2d);
    prepare_initial_d2u_ = std::move(initial_d2u);
    return prepare_result_;
  }
  void setBounds(absl::optional<uint64_t> u2d_limit, absl::optional<uint64_t> d2u_limit) override {
    set_bounds_called_ = true;
    u2d_limit_ = u2d_limit;
    d2u_limit_ = d2u_limit;
  }
  void arm() override { arm_called_ = true; }

  // Fixture knobs.
  bool create_pipes_result_{true};
  bool prepare_result_{true};

  // Observations.
  bool create_pipes_called_{false};
  bool prepare_called_{false};
  bool set_bounds_called_{false};
  bool arm_called_{false};
  bool need_u2d_{false};
  bool need_d2u_{false};
  std::string prepare_initial_u2d_;
  std::string prepare_initial_d2u_;
  absl::optional<uint64_t> u2d_limit_;
  absl::optional<uint64_t> d2u_limit_;
};

// GenericUpstream double exposing only the splice seams.
class TestGenericUpstream : public GenericUpstream {
public:
  // Splice seams under test.
  OptRef<Network::Connection> upstreamConnectionForSplice() override { return splice_connection_; }
  MOCK_METHOD(void, completeSplicedResponse, (uint64_t response_body_bytes), (override));

  // Unused GenericUpstream surface.
  void encodeData(Buffer::Instance&, bool) override {}
  void encodeMetadata(const Http::MetadataMapVector&) override {}
  Http::Status encodeHeaders(const Http::RequestHeaderMap&, bool) override {
    return Http::okStatus();
  }
  void encodeTrailers(const Http::RequestTrailerMap&) override {}
  void enableTcpTunneling() override {}
  void readDisable(bool) override {}
  void resetStream() override {}
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  OptRef<Network::Connection> splice_connection_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
};

// Downstream callbacks mock with splice seams.
class TestDownstreamFilterCallbacks : public Http::MockDownstreamStreamFilterCallbacks {
public:
  MOCK_METHOD(OptRef<Network::Connection>, downstreamConnectionForSplice, (), (override));
  MOCK_METHOD(void, completeSplicedRequest, (uint64_t request_body_bytes), (override));
};

} // namespace

// The fixture lives in Envoy::Router so UpstreamRequest friendship applies.
class SpliceCoordinatorTest : public testing::Test {
public:
  SpliceCoordinatorTest() {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.http1_ktls_body_splice", "true"}});
    HttpTestUtility::addDefaultHeaders(downstream_request_header_map_);
    ON_CALL(parent_, downstreamHeaders()).WillByDefault(Return(&downstream_request_header_map_));
  }

protected:
  // Builds the request, coordinator, mocks, and fake-pump factory.
  void initialize() {
    auto conn_pool = std::make_unique<NiceMock<MockGenericConnPool>>();
    conn_pool_ = conn_pool.get();
    ON_CALL(*conn_pool_, host()).WillByDefault(Return(host_));
    upstream_request_ = std::make_unique<UpstreamRequest>(parent_, std::move(conn_pool),
                                                          /*can_send_early_data=*/false,
                                                          /*can_use_http3=*/true,
                                                          /*enable_half_close=*/false);

    coord_ = std::make_unique<SpliceCoordinator>(*upstream_request_);

    // Capture callbacks in coordinator creation order.
    Event::MockDispatcher& disp = parent_.callbacks_.dispatcher_;
    ON_CALL(disp, createSchedulableCallback_(_))
        .WillByDefault(Invoke([this](std::function<void()> cb) -> Event::SchedulableCallback* {
          auto* sched = new NiceMock<Event::MockSchedulableCallback>(
              &this->parent_.callbacks_.dispatcher_, cb);
          if (engage_sched_ == nullptr) {
            engage_sched_ = sched;
            engage_cb_ = cb;
          } else {
            finalize_sched_ = sched;
            finalize_cb_ = cb;
          }
          return sched;
        }));
    // Classify timers by duration because creation order differs by direction.
    ON_CALL(disp, createTimer_(_)).WillByDefault(Invoke([this](Event::TimerCb cb) -> Event::Timer* {
      auto* timer = new NiceMock<Event::MockTimer>();
      timer->callback_ = cb;
      ON_CALL(*timer, enableTimer(_, _))
          .WillByDefault(
              Invoke([this, timer](std::chrono::milliseconds d, const ScopeTrackedObject* scope) {
                timer->enabled_ = true;
                timer->scope_ = scope;
                if (d == std::chrono::milliseconds(2)) {
                  poll_timer_ = timer;
                } else {
                  watchdog_timer_ = timer;
                }
              }));
      return timer;
    }));

    // Downstream leg resolved through downstream callbacks.
    ON_CALL(down_cb_, downstreamConnectionForSplice())
        .WillByDefault(Return(OptRef<Network::Connection>(downstream_conn_)));
    ON_CALL(parent_.callbacks_, downstreamCallbacks())
        .WillByDefault(Return(OptRef<Http::DownstreamStreamFilterCallbacks>(down_cb_)));

    // Downstream defaults to plaintext.
    wireConnection(downstream_conn_, down_socket_, down_io_, down_socket_ptr_, kDownstreamFd);
    ON_CALL(downstream_conn_, ssl()).WillByDefault(Return(nullptr));
    ON_CALL(downstream_conn_, ktlsBytestreamInfo())
        .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>{}));

    // Upstream defaults to trusted kTLS.
    wireConnection(upstream_conn_, up_socket_, up_io_, up_socket_ptr_, kUpstreamFd);
    ON_CALL(upstream_conn_, ssl()).WillByDefault(Return(nullptr));
    up_ktls_.installed = true;
    up_ktls_.trusted_peer = true;
    ON_CALL(upstream_conn_, ktlsBytestreamInfo())
        .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>(up_ktls_)));

    // GenericUpstream resolves to upstream_conn_.
    auto test_upstream = std::make_unique<NiceMock<TestGenericUpstream>>();
    test_upstream_ = test_upstream.get();
    test_upstream_->splice_connection_ = upstream_conn_;
    upstream_request_->upstream_ = std::move(test_upstream);

    // Pending writes handed to the pump.
    ON_CALL(upstream_conn_, extractPendingWriteForSplice())
        .WillByDefault(Return(std::string("UP-HEADERS")));
    ON_CALL(downstream_conn_, extractPendingWriteForSplice())
        .WillByDefault(Return(std::string("DOWN-HEADERS")));

    // Capture the fake pump and its callbacks for each test.
    coord_->setSplicePumpFactoryForTest(
        [this](os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls, Event::Dispatcher& dispatcher,
               TcpProxy::SplicePump::CompletionCb on_complete,
               TcpProxy::SplicePump::BytesCb on_u2d_bytes,
               TcpProxy::SplicePump::BytesCb on_d2u_bytes) -> TcpProxy::SplicePumpPtr {
          factory_called_ = true;
          factory_down_fd_ = down_fd;
          factory_up_fd_ = up_fd;
          factory_up_is_ktls_ = up_is_ktls;
          completion_cb_ = std::move(on_complete);
          u2d_bytes_cb_ = std::move(on_u2d_bytes);
          d2u_bytes_cb_ = std::move(on_d2u_bytes);
          auto pump = std::make_unique<FakeSplicePump>(
              down_fd, up_fd, up_is_ktls, dispatcher, completion_cb_, u2d_bytes_cb_, d2u_bytes_cb_);
          pump->create_pipes_result_ = create_pipes_result_;
          pump->prepare_result_ = prepare_result_;
          fake_pump_ = pump.get();
          return pump;
        });
  }

  void TearDown() override {
    // Release non-owning wrappers around stack mocks.
    up_socket_ptr_.release();
    down_socket_ptr_.release();
  }

  // Wires a connection mock so getSocket()->ioHandle().fdDoNotUse() returns `fd`, resetFileEvents()
  // is a no-op, and a non-internal local address is present so spliceableFd accepts it.
  void wireConnection(NiceMock<Network::MockConnection>& c,
                      NiceMock<Network::MockConnectionSocket>& s,
                      NiceMock<Network::MockIoHandle>& io, Network::ConnectionSocketPtr& sptr,
                      os_fd_t fd) {
    sptr.reset(&s);
    ON_CALL(c, getSocket()).WillByDefault(ReturnRef(sptr));
    ON_CALL(s, ioHandle()).WillByDefault(ReturnRef(io));
    ON_CALL(Const(s), ioHandle()).WillByDefault(ReturnRef(io));
    ON_CALL(io, fdDoNotUse()).WillByDefault(Return(fd));
    c.connectionInfoSetter().setLocalAddress(
        Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:80"));
    // state() defaults Open via initializeMockConnection, readDisable returns a status.
    ON_CALL(c, readDisable(_))
        .WillByDefault(Return(Network::Connection::ReadDisableStatus::StillReadDisabled));
  }

  // Arms a download splice for `cl` bytes. encodeComplete() must already be true (set via the
  // friend). Returns the arm result.
  bool armDownload(uint64_t cl = kBodyBytes) {
    setEncodeComplete(true);
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-length", absl::StrCat(cl)}};
    return coord_->maybeArmForResponse(headers, /*end_stream=*/false);
  }

  // Arms an upload splice for `cl` bytes.
  bool armUpload(uint64_t cl = kBodyBytes) {
    Http::TestRequestHeaderMapImpl headers{{":method", "PUT"},
                                           {":path", "/"},
                                           {":authority", "host"},
                                           {"content-length", absl::StrCat(cl)}};
    return coord_->maybeArmForRequest(headers, /*end_stream=*/false);
  }

  void fireEngage() {
    ASSERT_TRUE(engage_cb_ != nullptr);
    engage_cb_();
  }
  void fireFinalize() {
    ASSERT_TRUE(finalize_cb_ != nullptr);
    finalize_cb_();
  }

  uint64_t counter(absl::string_view name) {
    return parent_.cluster_info_->stats_store_.counter(absl::StrCat("http1_ktls_splice.", name))
        .value();
  }

  // Private-member accessors. The fixture class is a friend of UpstreamRequest, but friendship is
  // not inherited by the per-TEST_F subclasses, so these helpers live here on the fixture and the
  // tests call them.
  void setEncodeComplete(bool complete) { upstream_request_->router_sent_end_stream_ = complete; }
  void clearUpstream() { upstream_request_->upstream_.reset(); }
  bool onResetStreamInProgress() const { return upstream_request_->on_reset_stream_in_progress_; }
  // Re-enters onResetStream as the codec reset cascade would when the upstream is force-closed.
  void reenterOnResetStream() {
    upstream_request_->onResetStream(Http::StreamResetReason::ConnectionTermination,
                                     "codec reset cascade");
  }

  TestScopedRuntime scoped_runtime_;
  NiceMock<MockRouterFilterInterface> parent_;
  Http::TestRequestHeaderMapImpl downstream_request_header_map_;
  MockGenericConnPool* conn_pool_{nullptr};
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::unique_ptr<UpstreamRequest> upstream_request_;
  std::unique_ptr<SpliceCoordinator> coord_;

  NiceMock<Network::MockConnection> upstream_conn_;
  NiceMock<Network::MockConnection> downstream_conn_;
  NiceMock<Network::MockConnectionSocket> up_socket_;
  NiceMock<Network::MockConnectionSocket> down_socket_;
  NiceMock<Network::MockIoHandle> up_io_;
  NiceMock<Network::MockIoHandle> down_io_;
  Network::ConnectionSocketPtr up_socket_ptr_;
  Network::ConnectionSocketPtr down_socket_ptr_;
  Network::KtlsBytestreamInfo up_ktls_;
  Network::KtlsBytestreamInfo down_ktls_;

  NiceMock<TestDownstreamFilterCallbacks> down_cb_;
  TestGenericUpstream* test_upstream_{nullptr};

  // Captured dispatcher artifacts.
  std::function<void()> engage_cb_;
  std::function<void()> finalize_cb_;
  Event::MockSchedulableCallback* engage_sched_{nullptr};
  Event::MockSchedulableCallback* finalize_sched_{nullptr};
  Event::MockTimer* poll_timer_{nullptr};
  Event::MockTimer* watchdog_timer_{nullptr};

  // Fake pump factory knobs and observations.
  bool create_pipes_result_{true};
  bool prepare_result_{true};
  bool factory_called_{false};
  os_fd_t factory_down_fd_{0};
  os_fd_t factory_up_fd_{0};
  bool factory_up_is_ktls_{false};
  FakeSplicePump* fake_pump_{nullptr};
  TcpProxy::SplicePump::CompletionCb completion_cb_;
  TcpProxy::SplicePump::BytesCb u2d_bytes_cb_;
  TcpProxy::SplicePump::BytesCb d2u_bytes_cb_;
};

TEST_F(SpliceCoordinatorTest, ArmDownloadHappy) {
  initialize();
  EXPECT_TRUE(armDownload());
  EXPECT_TRUE(coord_->armedForResponse());
  EXPECT_FALSE(coord_->armedForRequest());
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(0, counter("abandoned"));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadFlagOff) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.http1_ktls_body_splice", "false"}});
  initialize();
  EXPECT_FALSE(armDownload());
  EXPECT_FALSE(coord_->armedForResponse());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadEndStream) {
  initialize();
  setEncodeComplete(true);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-length", absl::StrCat(kBodyBytes)}};
  EXPECT_FALSE(coord_->maybeArmForResponse(headers, /*end_stream=*/true));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadEncodeIncomplete) {
  initialize();
  setEncodeComplete(false);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                          {"content-length", absl::StrCat(kBodyBytes)}};
  EXPECT_FALSE(coord_->maybeArmForResponse(headers, /*end_stream=*/false));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadNoContentLength) {
  initialize();
  setEncodeComplete(true);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  EXPECT_FALSE(coord_->maybeArmForResponse(headers, /*end_stream=*/false));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadContentLengthUnparseable) {
  initialize();
  setEncodeComplete(true);
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-length", "abc"}};
  EXPECT_FALSE(coord_->maybeArmForResponse(headers, /*end_stream=*/false));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadContentLengthBelowMin) {
  initialize();
  // Well below the 64 KiB minimum.
  EXPECT_FALSE(armDownload(1024));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadContentLengthAtMin) {
  initialize();
  EXPECT_TRUE(armDownload(65536));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadContentLengthJustBelowMin) {
  initialize();
  EXPECT_FALSE(armDownload(65535));
}

TEST_F(SpliceCoordinatorTest, ArmDownloadUpstreamNotBorrowable) {
  initialize();
  test_upstream_->splice_connection_ = {}; // Upstream connection is empty.
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadDownstreamNotBorrowable) {
  initialize();
  ON_CALL(down_cb_, downstreamConnectionForSplice())
      .WillByDefault(Return(OptRef<Network::Connection>{}));
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadUpstreamNoKtlsInfo) {
  initialize();
  ON_CALL(upstream_conn_, ktlsBytestreamInfo())
      .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>{}));
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadUpstreamKtlsNotInstalled) {
  initialize();
  up_ktls_.installed = false;
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadUpstreamKtlsUntrusted) {
  initialize();
  up_ktls_.trusted_peer = false;
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadSinkBoringSsl) {
  initialize();
  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(downstream_conn_, ssl()).WillByDefault(Return(ssl_info));
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadSinkUserspaceTlsTrap) {
  initialize();
  down_ktls_.installed = false;
  ON_CALL(downstream_conn_, ktlsBytestreamInfo())
      .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>(down_ktls_)));
  EXPECT_FALSE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadSinkKtlsInstalled) {
  initialize();
  down_ktls_.installed = true;
  ON_CALL(downstream_conn_, ktlsBytestreamInfo())
      .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>(down_ktls_)));
  EXPECT_TRUE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmDownloadSinkPlaintextRaw) {
  initialize();
  EXPECT_TRUE(armDownload());
}

TEST_F(SpliceCoordinatorTest, ArmUploadHappy) {
  initialize();
  EXPECT_CALL(downstream_conn_, readDisable(true))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::StillReadDisabled));
  EXPECT_TRUE(armUpload());
  EXPECT_TRUE(coord_->armedForRequest());
  EXPECT_FALSE(coord_->armedForResponse());
  EXPECT_EQ(0, counter("abandoned"));
}

TEST_F(SpliceCoordinatorTest, ArmUploadFlagOff) {
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.http1_ktls_body_splice", "false"}});
  initialize();
  EXPECT_CALL(downstream_conn_, readDisable(true)).Times(0);
  EXPECT_FALSE(armUpload());
}

TEST_F(SpliceCoordinatorTest, ArmUploadEndStreamBodyless) {
  initialize();
  EXPECT_CALL(downstream_conn_, readDisable(true)).Times(0);
  Http::TestRequestHeaderMapImpl headers{{":method", "PUT"},
                                         {":path", "/"},
                                         {":authority", "host"},
                                         {"content-length", absl::StrCat(kBodyBytes)}};
  EXPECT_FALSE(coord_->maybeArmForRequest(headers, /*end_stream=*/true));
}

TEST_F(SpliceCoordinatorTest, ArmUploadNoContentLength) {
  initialize();
  Http::TestRequestHeaderMapImpl headers{
      {":method", "PUT"}, {":path", "/"}, {":authority", "host"}};
  EXPECT_FALSE(coord_->maybeArmForRequest(headers, /*end_stream=*/false));
}

TEST_F(SpliceCoordinatorTest, ArmUploadContentLengthUnparseable) {
  initialize();
  Http::TestRequestHeaderMapImpl headers{
      {":method", "PUT"}, {":path", "/"}, {":authority", "host"}, {"content-length", "xyz"}};
  EXPECT_FALSE(coord_->maybeArmForRequest(headers, /*end_stream=*/false));
}

TEST_F(SpliceCoordinatorTest, ArmUploadContentLengthBelowMin) {
  initialize();
  EXPECT_FALSE(armUpload(65535));
}

TEST_F(SpliceCoordinatorTest, ArmUploadContentLengthAtMin) {
  initialize();
  EXPECT_CALL(downstream_conn_, readDisable(true))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::StillReadDisabled));
  EXPECT_TRUE(armUpload(65536));
}

TEST_F(SpliceCoordinatorTest, ArmUploadSourceNotBorrowable) {
  initialize();
  ON_CALL(down_cb_, downstreamConnectionForSplice())
      .WillByDefault(Return(OptRef<Network::Connection>{}));
  EXPECT_CALL(downstream_conn_, readDisable(true)).Times(0);
  EXPECT_FALSE(armUpload());
}

TEST_F(SpliceCoordinatorTest, ArmUploadSourceBoringSsl) {
  initialize();
  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(downstream_conn_, ssl()).WillByDefault(Return(ssl_info));
  EXPECT_FALSE(armUpload());
}

TEST_F(SpliceCoordinatorTest, ArmUploadSourceUserspaceTlsTrap) {
  initialize();
  down_ktls_.installed = false;
  ON_CALL(downstream_conn_, ktlsBytestreamInfo())
      .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>(down_ktls_)));
  EXPECT_FALSE(armUpload());
}

TEST_F(SpliceCoordinatorTest, ArmUploadSourceRaw) {
  initialize();
  EXPECT_CALL(downstream_conn_, readDisable(true))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::StillReadDisabled));
  EXPECT_TRUE(armUpload());
}

TEST_F(SpliceCoordinatorTest, BufferTakeBody) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl data("0123456789");
  EXPECT_TRUE(coord_->bufferPreEngageBody(data, /*end_stream=*/false));
  EXPECT_EQ(0, data.length());
  EXPECT_EQ(0, counter("abandoned"));
  EXPECT_TRUE(coord_->armedForResponse());
}

TEST_F(SpliceCoordinatorTest, BufferMessageEndsBeforeEngageDownload) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl held("held");
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  Buffer::OwnedImpl data("final");
  EXPECT_CALL(parent_, onUpstreamData(_, _, false));
  EXPECT_FALSE(coord_->bufferPreEngageBody(data, /*end_stream=*/true));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_FALSE(coord_->armedForResponse());
}

TEST_F(SpliceCoordinatorTest, BufferExceedsMaxHeld) {
  initialize();
  ASSERT_TRUE(armDownload());
  // Hold just under the 4 MiB cap, then push it over.
  Buffer::OwnedImpl held(std::string(4 * 1024 * 1024 - 10, 'x'));
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  Buffer::OwnedImpl data(std::string(20, 'y'));
  EXPECT_CALL(parent_, onUpstreamData(_, _, false));
  EXPECT_FALSE(coord_->bufferPreEngageBody(data, /*end_stream=*/false));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, BufferAtMaxHeldBoundary) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl data(std::string(4 * 1024 * 1024, 'z'));
  EXPECT_TRUE(coord_->bufferPreEngageBody(data, /*end_stream=*/false));
  EXPECT_EQ(0, data.length());
  EXPECT_EQ(0, counter("abandoned"));
}

TEST_F(SpliceCoordinatorTest, EngageDownloadSuccess) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  // A download must not disable retries.
  EXPECT_CALL(parent_, disableRetries()).Times(0);
  fireEngage();
  EXPECT_TRUE(coord_->engaged());
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(0, counter("abandoned"));
  ASSERT_NE(nullptr, fake_pump_);
  // Download uses the u2d pipe only and bounds the u2d direction.
  EXPECT_TRUE(fake_pump_->need_u2d_);
  EXPECT_FALSE(fake_pump_->need_d2u_);
  ASSERT_TRUE(fake_pump_->u2d_limit_.has_value());
  EXPECT_EQ(kBodyBytes, fake_pump_->u2d_limit_.value());
  EXPECT_FALSE(fake_pump_->d2u_limit_.has_value());
  EXPECT_TRUE(fake_pump_->arm_called_);
  // up_is_ktls is always true on the download upstream leg.
  EXPECT_TRUE(factory_up_is_ktls_);
  // Watchdog armed at engage.
  ASSERT_NE(nullptr, watchdog_timer_);
  EXPECT_TRUE(watchdog_timer_->enabled_);
}

TEST_F(SpliceCoordinatorTest, EngageDownloadLegLostAfterSchedule) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  test_upstream_->splice_connection_ = {}; // Upstream leg is now empty.
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageDownloadRevalidateKtlsLost) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  up_ktls_.installed = false; // kTLS lost across the schedule gap
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageDownloadFdNotSpliceable) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  upstream_conn_.state_ = Network::Connection::State::Closed;
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageDownloadWholeBodyBuffered) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl whole(std::string(kBodyBytes, 'b'));
  ASSERT_TRUE(coord_->bufferPreEngageBody(whole, /*end_stream=*/false));
  coord_->scheduleEngage();
  EXPECT_CALL(parent_, onUpstreamData(_, _, false)); // held delivered via flush
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageDownloadPipeCreateFails) {
  initialize();
  create_pipes_result_ = false;
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  // Pending write is not extracted when pipe creation fails before the irreversible drain.
  EXPECT_CALL(downstream_conn_, extractPendingWriteForSplice()).Times(0);
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
  ASSERT_NE(nullptr, fake_pump_);
  EXPECT_TRUE(fake_pump_->create_pipes_called_);
  EXPECT_FALSE(fake_pump_->prepare_called_);
}

TEST_F(SpliceCoordinatorTest, EngageDownloadPrepareFailsAfterExtract) {
  initialize();
  prepare_result_ = false;
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  EXPECT_CALL(parent_, onUpstreamReset(Http::StreamResetReason::ConnectionTermination,
                                       "kTLS body-splice setup failed", _));
  fireEngage();
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(1, counter("truncated"));
  EXPECT_EQ(0, counter("abandoned"));
}

// Held bytes reduce the splice bound and follow the sink's pending headers.
TEST_F(SpliceCoordinatorTest, EngageDownloadPartialPreEngageBuffer) {
  initialize();
  ASSERT_TRUE(armDownload());
  constexpr uint64_t kHeld = 4096;
  Buffer::OwnedImpl held(std::string(kHeld, 'x'));
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  EXPECT_EQ(1, counter("engaged"));
  ASSERT_NE(nullptr, fake_pump_);
  // The bound is the Content-Length minus the body already held in memory.
  ASSERT_TRUE(fake_pump_->u2d_limit_.has_value());
  EXPECT_EQ(kBodyBytes - kHeld, fake_pump_->u2d_limit_.value());
  EXPECT_FALSE(fake_pump_->d2u_limit_.has_value());
  // The download pre-engage chunk is the sink's pending headers followed by the held body.
  EXPECT_EQ(absl::StrCat("DOWN-HEADERS", std::string(kHeld, 'x')),
            fake_pump_->prepare_initial_u2d_);
  EXPECT_TRUE(fake_pump_->prepare_initial_d2u_.empty());
}

// Sink accounting includes the full Content-Length while source accounting excludes held bytes.
TEST_F(SpliceCoordinatorTest, FinalizeDownloadAccountsFullContentLength) {
  initialize();
  auto meter = std::make_shared<StreamInfo::BytesMeter>();
  parent_.callbacks_.stream_info_.downstream_bytes_meter_ = meter;
  ASSERT_TRUE(armDownload());
  constexpr uint64_t kHeld = 4096;
  Buffer::OwnedImpl held(std::string(kHeld, 'x'));
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(*test_upstream_, completeSplicedResponse(kBodyBytes - kHeld));
  fireFinalize();
  EXPECT_EQ(kBodyBytes, meter->wireBytesSent());
}

// Upload sink accounting mirrors the download path.
TEST_F(SpliceCoordinatorTest, FinalizeUploadAccountsFullContentLength) {
  initialize();
  auto meter = std::make_shared<StreamInfo::BytesMeter>();
  parent_.callbacks_.stream_info_.upstream_bytes_meter_ = meter;
  ASSERT_TRUE(armUpload());
  constexpr uint64_t kHeld = 4096;
  Buffer::OwnedImpl held(std::string(kHeld, 'x'));
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(down_cb_, completeSplicedRequest(kBodyBytes - kHeld));
  fireFinalize();
  EXPECT_EQ(kBodyBytes, meter->wireBytesSent());
  EXPECT_EQ(kBodyBytes, upstream_request_->streamInfo().bytesSent());
}

// A leg with no kernel fd abandons.
TEST_F(SpliceCoordinatorTest, EngageDownloadFdInvalidSocket) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  ON_CALL(up_io_, fdDoNotUse()).WillByDefault(Return(INVALID_SOCKET));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
}

// An internal-listener leg has no kernel fd to splice on.
TEST_F(SpliceCoordinatorTest, EngageDownloadLegEnvoyInternal) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  upstream_conn_.connectionInfoSetter().setLocalAddress(
      std::make_shared<Network::Address::EnvoyInternalInstance>("test_internal"));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
}

// Engage re-validation rejects a sink that changes to userspace TLS.
TEST_F(SpliceCoordinatorTest, EngageDownloadSinkTlsFlipAfterSchedule) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  auto ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(downstream_conn_, ssl()).WillByDefault(Return(ssl_info));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
}

TEST_F(SpliceCoordinatorTest, EngageUploadShadowActive) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  ON_CALL(parent_, shadowStreamsActive()).WillByDefault(Return(true));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageUploadDownstreamLost) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  ON_CALL(down_cb_, downstreamConnectionForSplice())
      .WillByDefault(Return(OptRef<Network::Connection>{}));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageUploadPoolNotReadyPoll) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  clearUpstream(); // Pool is not ready.
  test_upstream_ = nullptr;
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("abandoned"));
  ASSERT_NE(nullptr, poll_timer_);
  EXPECT_TRUE(poll_timer_->enabled_); // rescheduled on the 2ms poll timer
}

TEST_F(SpliceCoordinatorTest, EngageUploadUpstreamNotHttp1) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  test_upstream_->splice_connection_ = {}; // present but cannot be borrowed
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
  // Did not create a poll timer.
  EXPECT_EQ(nullptr, poll_timer_);
}

TEST_F(SpliceCoordinatorTest, EngageUploadUpstreamNoKtls) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  ON_CALL(upstream_conn_, ktlsBytestreamInfo())
      .WillByDefault(Return(OptRef<const Network::KtlsBytestreamInfo>{}));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageUploadKtlsInstallingPoll) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  up_ktls_.installed = false; // kTLS-TX still installing
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("abandoned"));
  ASSERT_NE(nullptr, poll_timer_);
  EXPECT_TRUE(poll_timer_->enabled_);
}

TEST_F(SpliceCoordinatorTest, EngageUploadPollExceedsMax) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  up_ktls_.installed = false; // keep polling forever
  fireEngage();               // engage_polls_ -> 1, schedules poll timer
  ASSERT_NE(nullptr, poll_timer_);
  // Fire the poll timer until the bound is exceeded. The first fireEngage already did poll 1.
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
  // Sixty-four more poll-timer fires bring engage_polls_ from 1 to 65, past the bound.
  for (int i = 0; i < 64; i++) {
    ASSERT_TRUE(poll_timer_->enabled_);
    poll_timer_->invokeCallback();
  }
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, EngageUploadSuccess) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  // Upload must disable retries the moment it engages.
  EXPECT_CALL(parent_, disableRetries());
  fireEngage();
  EXPECT_TRUE(coord_->engaged());
  EXPECT_EQ(1, counter("engaged"));
  ASSERT_NE(nullptr, fake_pump_);
  // Upload uses the d2u pipe only and bounds the d2u direction.
  EXPECT_FALSE(fake_pump_->need_u2d_);
  EXPECT_TRUE(fake_pump_->need_d2u_);
  ASSERT_TRUE(fake_pump_->d2u_limit_.has_value());
  EXPECT_EQ(kBodyBytes, fake_pump_->d2u_limit_.value());
  EXPECT_FALSE(fake_pump_->u2d_limit_.has_value());
  ASSERT_NE(nullptr, watchdog_timer_);
  EXPECT_TRUE(watchdog_timer_->enabled_);
}

TEST_F(SpliceCoordinatorTest, EngageNotArmedNoop) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  coord_->reset(); // clears armed_
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_FALSE(factory_called_);
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, CompleteDownloadBoundsReached) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  // onSpliceComplete only schedules finalize. Pump not yet gone.
  EXPECT_TRUE(coord_->engaged());

  {
    InSequence seq;
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(*test_upstream_, completeSplicedResponse(kBodyBytes));
    EXPECT_CALL(upstream_conn_, reinstallFileEvents());
  }
  // Neither leg is closed on a clean completion.
  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  EXPECT_CALL(downstream_conn_, close(_)).Times(0);
  fireFinalize();
  EXPECT_FALSE(coord_->engaged());
  // Per-engaged invariant, one engaged, then completed or truncated.
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(1, counter("completed"));
  EXPECT_EQ(0, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, CompleteUploadBoundsReached) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  {
    InSequence seq;
    // Re-arm upload sink before re-enabling the source and completing.
    EXPECT_CALL(upstream_conn_, reinstallFileEvents());
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(downstream_conn_, readDisable(false))
        .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
    EXPECT_CALL(down_cb_, completeSplicedRequest(kBodyBytes));
  }
  fireFinalize();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(1, counter("completed"));
  EXPECT_EQ(0, counter("truncated"));
}

// Truncation must reinstall the downstream sink before force-closing upstream.
TEST_F(SpliceCoordinatorTest, CompleteTruncated) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::Closed);
  {
    InSequence seq;
    // The download sink leg is reinstalled before the upstream close cascade can reach it.
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush));
  }
  // The upstream is force-closed and never re-armed for reuse.
  EXPECT_CALL(upstream_conn_, reinstallFileEvents()).Times(0);
  fireFinalize();
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(1, counter("truncated"));
  EXPECT_EQ(0, counter("completed"));
}

// A closed upstream is not force-closed again, but truncation is still counted.
TEST_F(SpliceCoordinatorTest, CompleteTruncatedUpstreamAlreadyClosed) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::Closed);
  upstream_conn_.state_ = Network::Connection::State::Closed;
  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  // The downstream is still Open, so it is reinstalled even though the upstream close is skipped.
  EXPECT_CALL(downstream_conn_, reinstallFileEvents());
  fireFinalize();
  EXPECT_EQ(1, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, CompleteErrorRoutesToTruncation) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  // Upload that truncates re-enables the held source before force-closing the upstream.
  completion_cb_(TcpProxy::SpliceCompletion::Closed);
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush));
  fireFinalize();
  EXPECT_EQ(1, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, CompleteFinalizeDeferred) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  // The completion callback must not destroy the pump inline. engaged() stays true until finalize.
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_TRUE(coord_->engaged());
  ASSERT_TRUE(finalize_cb_ != nullptr);
  EXPECT_TRUE(finalize_sched_->enabled_);
  fireFinalize();
  EXPECT_FALSE(coord_->engaged());
}

// reset() also reinstalls the downstream sink before force-closing upstream.
TEST_F(SpliceCoordinatorTest, ResetWhileEngaged) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  {
    InSequence seq;
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush));
  }
  coord_->reset();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(1, counter("truncated"));
  // The latch is set before the close so a re-entrant onResetStream is a no-op.
  EXPECT_TRUE(onResetStreamInProgress());
}

// BoundsReached before reset counts completed even when teardown wins the race.
TEST_F(SpliceCoordinatorTest, ResetAfterBoundsReachedCountsCompleted) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush));
  coord_->reset();
  EXPECT_EQ(1, counter("engaged"));
  EXPECT_EQ(1, counter("completed"));
  EXPECT_EQ(0, counter("truncated"));
}

// Proves the downstream reinstall happens before the upstream close callback.
TEST_F(SpliceCoordinatorTest, ResetDownloadReinstallsSinkBeforeUpstreamClose) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  bool sink_reinstalled = false;
  bool reinstalled_before_close = false;
  ON_CALL(downstream_conn_, reinstallFileEvents()).WillByDefault(Invoke([&]() {
    sink_reinstalled = true;
  }));
  // The sink reinstall must already have run when upstream closes.
  ON_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush)).WillByDefault(Invoke([&]() {
    reinstalled_before_close = sink_reinstalled;
  }));
  EXPECT_CALL(downstream_conn_, reinstallFileEvents()).Times(1);
  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  // The upstream is force-closed, never re-armed.
  EXPECT_CALL(upstream_conn_, reinstallFileEvents()).Times(0);

  coord_->reset();

  EXPECT_TRUE(sink_reinstalled);
  EXPECT_TRUE(reinstalled_before_close);
  EXPECT_EQ(1, counter("truncated"));
}

// NoFlush close re-enters onResetStream, which must stop at the reset guard.
TEST_F(SpliceCoordinatorTest, ResetDoubleFreeGuard) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  bool latch_set_at_close = false;
  int close_reentries = 0;
  // Re-enter onResetStream from the close callback to exercise the guard.
  ON_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush)).WillByDefault(Invoke([&]() {
    ++close_reentries;
    latch_set_at_close = onResetStreamInProgress();
    reenterOnResetStream();
  }));
  // The nested onResetStream must short-circuit at the latch.
  EXPECT_CALL(parent_, onUpstreamReset(_, _, _)).Times(0);
  // The upstream is force-closed exactly once.
  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  // The latch is clear before reset(), proving reset() itself sets it (not fixture init).
  EXPECT_FALSE(onResetStreamInProgress());
  // Drive the in-flight reset, simulating UpstreamRequest teardown reaching the coordinator.
  coord_->reset();

  // The close fired and re-entered onResetStream exactly once. The cascade really ran.
  EXPECT_EQ(1, close_reentries);
  EXPECT_TRUE(latch_set_at_close); // Latch was set before the close.
  EXPECT_EQ(1, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, ResetNotEngagedNoTruncated) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  fireFinalize(); // Clean completion cleared the pump.
  ASSERT_EQ(1, counter("completed"));

  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  coord_->reset();
  // After a clean finalize the pump is already gone, so reset() does not count a truncation.
  EXPECT_EQ(0, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, ResetIdempotent) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());

  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  // The download sink is reinstalled exactly once. The first reset clears the connection refs, so
  // the second reset's reinstall guard is false.
  EXPECT_CALL(downstream_conn_, reinstallFileEvents()).Times(1);
  coord_->reset();
  coord_->reset(); // second reset is a no-op, no second close, no second truncated
  EXPECT_EQ(1, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, ResetArmedNotEngaged) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  coord_->reset();
  EXPECT_FALSE(coord_->armedForResponse());
  EXPECT_EQ(0, counter("truncated"));
  EXPECT_EQ(0, counter("abandoned"));
}

TEST_F(SpliceCoordinatorTest, ResetAfterFinalize) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .WillRepeatedly(Return(Network::Connection::ReadDisableStatus::NoTransition));
  fireFinalize();
  ASSERT_EQ(1, counter("completed"));

  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  coord_->reset();
  EXPECT_EQ(0, counter("truncated"));
}

// The pump is destroyed (engaged() false) before any leg re-arm.
TEST_F(SpliceCoordinatorTest, FinalizePumpDestroyedBeforeRearm) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);

  bool pump_gone_at_first_rearm = false;
  ON_CALL(downstream_conn_, reinstallFileEvents()).WillByDefault(Invoke([&]() {
    pump_gone_at_first_rearm = !coord_->engaged();
  }));
  EXPECT_CALL(*test_upstream_, completeSplicedResponse(_));
  fireFinalize();
  EXPECT_TRUE(pump_gone_at_first_rearm);
}

TEST_F(SpliceCoordinatorTest, FinalizeDownloadRearmOrder) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  {
    InSequence seq;
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(*test_upstream_, completeSplicedResponse(kBodyBytes));
    EXPECT_CALL(upstream_conn_, reinstallFileEvents());
  }
  fireFinalize();
}

// Connection members are cleared before completeSpliced* can defer-delete the coordinator.
TEST_F(SpliceCoordinatorTest, FinalizeCapturesRefsOnStack) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);

  EXPECT_CALL(*test_upstream_, completeSplicedResponse(_)).WillOnce(Invoke([&](uint64_t) {
    // Simulate teardown re-entering reset() from inside the codec finalize. With the members
    // already cleared, this must not re-close or re-truncate.
    coord_->reset();
  }));
  // The upstream is re-armed (not closed) on the clean path. The nested reset finds no refs so it
  // does not close.
  EXPECT_CALL(upstream_conn_, close(_)).Times(0);
  fireFinalize();
  EXPECT_EQ(0, counter("truncated"));
  EXPECT_EQ(1, counter("completed"));
}

TEST_F(SpliceCoordinatorTest, FinalizeLegClosedSkipRearm) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);

  upstream_conn_.state_ = Network::Connection::State::Closed; // captured upstream not Open
  EXPECT_CALL(downstream_conn_, reinstallFileEvents());
  EXPECT_CALL(*test_upstream_, completeSplicedResponse(_));
  EXPECT_CALL(upstream_conn_, reinstallFileEvents()).Times(0); // closed leg not re-armed
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, FinalizeReadEnableSourceReinstallFirst) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  {
    InSequence seq;
    // Upstream sink re-armed first.
    EXPECT_CALL(upstream_conn_, reinstallFileEvents());
    // Then the held source is re-enabled by reinstalling the source file event before
    // readDisable(false).
    EXPECT_CALL(downstream_conn_, reinstallFileEvents());
    EXPECT_CALL(downstream_conn_, readDisable(false))
        .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
    EXPECT_CALL(down_cb_, completeSplicedRequest(_));
  }
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, WatchdogRearmedOnProgress) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_NE(nullptr, watchdog_timer_);

  // resetIdleTimer on the downstream HCM callbacks is refreshed on progress.
  EXPECT_CALL(parent_.callbacks_, resetIdleTimer());
  // The no-progress watchdog is re-armed at its 30-second timeout on every byte callback.
  EXPECT_CALL(*watchdog_timer_, enableTimer(std::chrono::milliseconds(30000), _));
  // A byte callback drives onSpliceProgress.
  ASSERT_TRUE(u2d_bytes_cb_ != nullptr);
  u2d_bytes_cb_(4096);
}

TEST_F(SpliceCoordinatorTest, WatchdogFiresOnStall) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_NE(nullptr, watchdog_timer_);

  // The watchdog callback resets the stream with the stall reason. It does not count the truncation
  // itself. That happens when teardown reaches reset().
  EXPECT_CALL(parent_, onUpstreamReset(Http::StreamResetReason::ConnectionTermination,
                                       "kTLS body-splice stalled", _));
  ASSERT_TRUE(watchdog_timer_->enabled_);
  watchdog_timer_->invokeCallback();
  EXPECT_EQ(0, counter("truncated"));
}

TEST_F(SpliceCoordinatorTest, WatchdogArmedAtEngage) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_NE(nullptr, watchdog_timer_);
  EXPECT_TRUE(watchdog_timer_->enabled_);
}

TEST_F(SpliceCoordinatorTest, WatchdogDisabledOnFinalize) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_NE(nullptr, watchdog_timer_);
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(*test_upstream_, completeSplicedResponse(_));
  EXPECT_CALL(*watchdog_timer_, disableTimer());
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, WatchdogDisabledOnReset) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_NE(nullptr, watchdog_timer_);
  EXPECT_CALL(upstream_conn_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*watchdog_timer_, disableTimer());
  coord_->reset();
}

TEST_F(SpliceCoordinatorTest, ReadEnableDownloadNoop) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  fireEngage();
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(*test_upstream_, completeSplicedResponse(_));
  EXPECT_CALL(downstream_conn_, readDisable(false)).Times(0);
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, ReadEnableUploadOnce) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .Times(1)
      .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
  EXPECT_CALL(down_cb_, completeSplicedRequest(_));
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, ReadEnableSourceClosedSkip) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  downstream_conn_.state_ = Network::Connection::State::Closed;
  EXPECT_CALL(downstream_conn_, readDisable(false)).Times(0);
  EXPECT_CALL(down_cb_, completeSplicedRequest(_));
  fireFinalize();
}

TEST_F(SpliceCoordinatorTest, ReadEnableNotDetachedNoReinstall) {
  initialize();
  ASSERT_TRUE(armUpload());
  // Abandon before engage (message ends), so legs were never detached.
  Buffer::OwnedImpl data("x");
  EXPECT_CALL(downstream_conn_, reinstallFileEvents()).Times(0);
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .WillOnce(Return(Network::Connection::ReadDisableStatus::NoTransition));
  EXPECT_FALSE(coord_->bufferPreEngageBody(data, /*end_stream=*/true));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, FlushDownloadPath) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl held("held-body");
  ASSERT_TRUE(coord_->bufferPreEngageBody(held, /*end_stream=*/false));
  Buffer::OwnedImpl end;
  EXPECT_CALL(parent_, onUpstreamData(_, _, false));
  EXPECT_FALSE(coord_->bufferPreEngageBody(end, /*end_stream=*/true));
}

TEST_F(SpliceCoordinatorTest, FlushEmptyNoop) {
  initialize();
  ASSERT_TRUE(armDownload());
  Buffer::OwnedImpl end;
  EXPECT_CALL(parent_, onUpstreamData(_, _, _)).Times(0);
  EXPECT_FALSE(coord_->bufferPreEngageBody(end, /*end_stream=*/true));
  EXPECT_EQ(1, counter("abandoned"));
  EXPECT_EQ(0, counter("engaged"));
}

TEST_F(SpliceCoordinatorTest, DisarmCancelsCallbacks) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  ASSERT_NE(nullptr, engage_sched_);
  EXPECT_CALL(*engage_sched_, cancel());
  coord_->reset(); // Reaches the disarm-equivalent cancel path.
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_FALSE(factory_called_);
}

TEST_F(SpliceCoordinatorTest, ScheduleNotArmedNoop) {
  initialize();
  // Not armed, so scheduleEngage creates no callback.
  coord_->scheduleEngage();
  EXPECT_EQ(nullptr, engage_sched_);
  EXPECT_FALSE(static_cast<bool>(engage_cb_));
}

TEST_F(SpliceCoordinatorTest, ScheduleCreatesCallbackOnce) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  ASSERT_NE(nullptr, engage_sched_);
  Event::MockSchedulableCallback* first = engage_sched_;
  EXPECT_CALL(*first, scheduleCallbackCurrentIteration()).Times(AnyNumber());
  coord_->scheduleEngage();
  // Still the same callback object. No second engage callback was created.
  EXPECT_EQ(first, engage_sched_);
}

// Downstream with no kernel fd mirrors the upstream INVALID_SOCKET test.
TEST_F(SpliceCoordinatorTest, EngageDownloadDownFdInvalidSocket) {
  initialize();
  ASSERT_TRUE(armDownload());
  coord_->scheduleEngage();
  ON_CALL(down_io_, fdDoNotUse()).WillByDefault(Return(INVALID_SOCKET));
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("engaged"));
  EXPECT_EQ(1, counter("abandoned"));
}

// Installed but untrusted kTLS keeps the upload polling.
TEST_F(SpliceCoordinatorTest, EngageUploadUntrustedPeerPolls) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  up_ktls_.installed = true;
  up_ktls_.trusted_peer = false;
  fireEngage();
  EXPECT_FALSE(coord_->engaged());
  EXPECT_EQ(0, counter("abandoned"));
  ASSERT_NE(nullptr, poll_timer_);
  EXPECT_TRUE(poll_timer_->enabled_);
}

// Reset while upload is polling disables the poll timer.
TEST_F(SpliceCoordinatorTest, ResetDuringPollDisablesTimer) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  up_ktls_.installed = false; // keep polling
  fireEngage();
  ASSERT_NE(nullptr, poll_timer_);
  EXPECT_CALL(*poll_timer_, disableTimer());
  coord_->reset();
  EXPECT_FALSE(coord_->engaged());
  // No splice was in flight, so nothing is counted truncated.
  EXPECT_EQ(0, counter("truncated"));
}

// Upload finalize skips completeSplicedRequest when downstream callbacks are gone.
TEST_F(SpliceCoordinatorTest, CompleteUploadNoDownstreamCallbacks) {
  initialize();
  ASSERT_TRUE(armUpload());
  coord_->scheduleEngage();
  fireEngage();
  ASSERT_TRUE(coord_->engaged());
  completion_cb_(TcpProxy::SpliceCompletion::BoundsReached);
  // The downstream callbacks go away before finalize runs.
  ON_CALL(parent_.callbacks_, downstreamCallbacks())
      .WillByDefault(Return(OptRef<Http::DownstreamStreamFilterCallbacks>{}));
  EXPECT_CALL(down_cb_, completeSplicedRequest(_)).Times(0);
  EXPECT_CALL(downstream_conn_, readDisable(false))
      .WillRepeatedly(Return(Network::Connection::ReadDisableStatus::NoTransition));
  fireFinalize();
  EXPECT_EQ(1, counter("completed"));
}

} // namespace Router
} // namespace Envoy
