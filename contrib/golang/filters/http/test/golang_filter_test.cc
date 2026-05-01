#include <cstdint>
#include <memory>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "contrib/golang/common/dso/test/mocks.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
#include "gmock/gmock.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

class TestFilter : public Filter {
public:
  using Filter::continueStatus;
  using Filter::continueStatusInternal;
  using Filter::Filter;
  using Filter::sendLocalReply;
  DecodingProcessorState& testDecodingState() { return decoding_state_; }
  HttpRequestInternal* testReq() { return req_; }
};

namespace {

class GolangHttpFilterTest : public testing::Test {
public:
  GolangHttpFilterTest() {
    cluster_manager_.initializeThreadLocalClusters({"cluster"});

    // Avoid strict mock failures for the following calls. We want strict for other calls.
    EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _))
        .Times(AtLeast(0))
        .WillRepeatedly(Invoke([this](Buffer::Instance& data, bool) {
          if (decoder_callbacks_.buffer_ == nullptr) {
            decoder_callbacks_.buffer_ = std::make_unique<Buffer::OwnedImpl>();
          }
          decoder_callbacks_.buffer_->move(data);
        }));

    EXPECT_CALL(decoder_callbacks_, activeSpan()).Times(AtLeast(0));
    EXPECT_CALL(decoder_callbacks_, decodingBuffer()).Times(AtLeast(0));
    EXPECT_CALL(decoder_callbacks_, route()).Times(AtLeast(0));

    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
        .Times(AtLeast(0))
        .WillRepeatedly(Invoke([this](Buffer::Instance& data, bool) {
          if (encoder_callbacks_.buffer_ == nullptr) {
            encoder_callbacks_.buffer_ = std::make_unique<Buffer::OwnedImpl>();
          }
          encoder_callbacks_.buffer_->move(data);
        }));
    EXPECT_CALL(encoder_callbacks_, activeSpan()).Times(AtLeast(0));
    EXPECT_CALL(encoder_callbacks_, encodingBuffer()).Times(AtLeast(0));
    EXPECT_CALL(decoder_callbacks_, streamInfo()).Times(testing::AnyNumber());
  }

  ~GolangHttpFilterTest() override {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
    Dso::DsoManager<Dso::HttpFilterDsoImpl>::cleanUpForTest();
  }

  void setup(const std::string& lib_id, const std::string& lib_path,
             const std::string& plugin_name) {
    const auto yaml_fmt = R"EOF(
    library_id: %s
    library_path: %s
    plugin_name: %s
    plugin_config:
      "@type": type.googleapis.com/xds.type.v3.TypedStruct
      type_url: typexx
      value:
          key: value
          int: 10
          invalid: "invalid"
    )EOF";

    auto yaml_string = absl::StrFormat(yaml_fmt, lib_id, lib_path, plugin_name);
    envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
    TestUtility::loadFromYaml(yaml_string, proto_config);

    envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute per_route_proto_config;
    setupDso(lib_id, lib_path, plugin_name);
    setupConfig(proto_config, per_route_proto_config, plugin_name);
    setupFilter(plugin_name);
  }

  std::string genSoPath() {
    return TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/plugins.so");
  }

  void setupDso(std::string id, std::string path, std::string plugin_name) {
    Dso::DsoManager<Dso::HttpFilterDsoImpl>::load(id, path, plugin_name);
  }

  void setupConfig(
      envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
      envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute& per_route_proto_config,
      std::string plugin_name) {
    // Setup filter config for Golang filter.
    config_ = std::make_shared<FilterConfig>(
        proto_config, Dso::DsoManager<Dso::HttpFilterDsoImpl>::getDsoByPluginName(plugin_name), "",
        context_);
    config_->newGoPluginConfig();
    // Setup per route config for Golang filter.
    per_route_config_ =
        std::make_shared<FilterConfigPerRoute>(per_route_proto_config, server_factory_context_);
  }

  void setupFilter(const std::string& plugin_name) {
    Event::SimulatedTimeSystem test_time;
    test_time.setSystemTime(std::chrono::microseconds(1583879145572237));

    filter_ = std::make_unique<TestFilter>(
        config_, Dso::DsoManager<Dso::HttpFilterDsoImpl>::getDsoByPluginName(plugin_name), 0);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupMetadata(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, metadata_);
    ON_CALL(*decoder_callbacks_.route_, metadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  Upstream::MockClusterManager cluster_manager_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<FilterConfigPerRoute> per_route_config_;
  std::unique_ptr<TestFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  envoy::config::core::v3::Metadata metadata_;
  std::shared_ptr<NiceMock<Envoy::Ssl::MockConnectionInfo>> ssl_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Tracing::MockSpan child_span_;
  Stats::TestUtil::TestStore stats_store_;

  const std::string PASSTHROUGH{"passthrough"};
  const std::string ROUTECONFIG{"routeconfig"};
};

// request that is headers only.
TEST_F(GolangHttpFilterTest, ScriptHeadersOnlyRequestHeadersOnly) {
  InSequence s;
  setup(PASSTHROUGH, genSoPath(), PASSTHROUGH);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.golang.errors").value());
}

// setHeader at wrong stage
TEST_F(GolangHttpFilterTest, SetHeaderAtWrongStage) {
  InSequence s;
  setup(PASSTHROUGH, genSoPath(), PASSTHROUGH);
  auto req = new HttpRequestInternal(*filter_);

  EXPECT_EQ(CAPINotInGo, filter_->setHeader(req->decodingState(), "foo", "bar", HeaderSet));

  delete req;
}

// invalid config for routeconfig filter
TEST_F(GolangHttpFilterTest, InvalidConfigForRouteConfigFilter) {
  InSequence s;
  EXPECT_THROW_WITH_REGEX(setup(ROUTECONFIG, genSoPath(), ROUTECONFIG), EnvoyException,
                          "golang filter failed to parse plugin config");
}

// Regression test for https://github.com/envoyproxy/envoy/issues/44320.
TEST_F(GolangHttpFilterTest, BufferedDataAfterDestroyDuringContinue) {
  auto dso_lib = std::make_shared<NiceMock<Dso::MockHttpFilterDsoImpl>>();
  ON_CALL(*dso_lib, envoyGoFilterNewHttpPluginConfig(_)).WillByDefault(Return(1));
  ON_CALL(*dso_lib, envoyGoFilterOnHttpHeader(_, _, _, _))
      .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Running)));

  bool destroyed = false;
  bool data_called_after_destroy = false;
  ON_CALL(*dso_lib, envoyGoFilterOnHttpData(_, _, _, _))
      .WillByDefault(Invoke(
          [&destroyed, &data_called_after_destroy](processState*, GoUint64, GoUint64, GoUint64) {
            if (destroyed) {
              data_called_after_destroy = true;
            }
            return static_cast<uint64_t>(GolangStatus::Continue);
          }));
  ON_CALL(*dso_lib, envoyGoFilterOnHttpDestroy(_, _))
      .WillByDefault(Invoke([&destroyed](httpRequest*, int) { destroyed = true; }));

  const auto yaml = R"EOF(
    library_id: test
    library_path: test
    plugin_name: test
    )EOF";
  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  auto config = std::make_shared<FilterConfig>(proto_config, dso_lib, "", mock_context);
  config->newGoPluginConfig();

  Network::Address::InstanceConstSharedPtr addr(
      (*Network::Address::PipeInstance::create("/test/test.sock")).release());
  NiceMock<Http::MockStreamDecoderFilterCallbacks> mock_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> mock_enc_callbacks;
  NiceMock<Envoy::Network::MockConnection> mock_connection;
  ON_CALL(mock_callbacks, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{mock_connection}));
  mock_connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
  mock_connection.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr);
  EXPECT_CALL(mock_callbacks.dispatcher_, isThreadSafe()).WillRepeatedly(Return(true));

  auto filter = std::make_shared<TestFilter>(config, dso_lib, 0);
  filter->setDecoderFilterCallbacks(mock_callbacks);
  filter->setEncoderFilterCallbacks(mock_enc_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl body("request body");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter->decodeData(body, false));

  EXPECT_CALL(mock_callbacks, continueDecoding()).WillOnce(Invoke([&filter]() {
    filter->onDestroy();
  }));

  filter->continueStatusInternal(filter->testDecodingState(), GolangStatus::Continue);

  EXPECT_FALSE(data_called_after_destroy)
      << "envoyGoFilterOnHttpData must not be called after onDestroy";

  ASSERT_NE(nullptr, filter->testReq());
  delete filter->testReq();
}

// Helper to set up a mock-based filter for dispatcher post tests.
struct MockFilterContext {
  std::shared_ptr<NiceMock<Dso::MockHttpFilterDsoImpl>> dso_lib;
  std::shared_ptr<FilterConfig> config;
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  Network::Address::InstanceConstSharedPtr addr;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> mock_callbacks;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> mock_enc_callbacks;
  NiceMock<Envoy::Network::MockConnection> mock_connection;
  std::shared_ptr<TestFilter> filter;

  void setup(bool thread_safe) {
    dso_lib = std::make_shared<NiceMock<Dso::MockHttpFilterDsoImpl>>();
    ON_CALL(*dso_lib, envoyGoFilterNewHttpPluginConfig(_)).WillByDefault(Return(1));
    ON_CALL(*dso_lib, envoyGoFilterOnHttpHeader(_, _, _, _))
        .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Running)));

    const auto yaml = R"EOF(
      library_id: test
      library_path: test
      plugin_name: test
      )EOF";
    envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    config = std::make_shared<FilterConfig>(proto_config, dso_lib, "", mock_context);
    config->newGoPluginConfig();

    addr.reset((*Network::Address::PipeInstance::create("/test/test.sock")).release());
    ON_CALL(mock_callbacks, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{mock_connection}));
    mock_connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
    mock_connection.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr);
    EXPECT_CALL(mock_callbacks.dispatcher_, isThreadSafe()).WillRepeatedly(Return(thread_safe));

    filter = std::make_shared<TestFilter>(config, dso_lib, 0);
    filter->setDecoderFilterCallbacks(mock_callbacks);
    filter->setEncoderFilterCallbacks(mock_enc_callbacks);

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter->decodeHeaders(request_headers, false));
  }

  void teardown() {
    filter->onDestroy();
    delete filter->testReq();
  }
};

// Verify that continueStatus executes inline when already on the worker thread,
// bypassing dispatcher.post for lower latency.
TEST_F(GolangHttpFilterTest, ContinueStatusSkipsPostOnWorkerThread) {
  MockFilterContext ctx;
  ctx.setup(true);

  EXPECT_CALL(ctx.mock_callbacks.dispatcher_, post(_)).Times(0);
  EXPECT_CALL(ctx.mock_callbacks, continueDecoding());

  auto status = ctx.filter->continueStatus(ctx.filter->testDecodingState(), GolangStatus::Continue);
  EXPECT_EQ(CAPIStatus::CAPIOK, status);

  ctx.teardown();
}

// Verify that continueStatus posts to the dispatcher when not on the worker thread.
TEST_F(GolangHttpFilterTest, ContinueStatusPostsOffWorkerThread) {
  MockFilterContext ctx;
  ctx.setup(false);

  EXPECT_CALL(ctx.mock_callbacks.dispatcher_, post(_));

  auto status = ctx.filter->continueStatus(ctx.filter->testDecodingState(), GolangStatus::Continue);
  EXPECT_EQ(CAPIStatus::CAPIOK, status);

  ctx.teardown();
}

// Verify that sendLocalReply executes inline when already on the worker thread.
TEST_F(GolangHttpFilterTest, SendLocalReplySkipsPostOnWorkerThread) {
  MockFilterContext ctx;
  ctx.setup(true);

  EXPECT_CALL(ctx.mock_callbacks.dispatcher_, post(_)).Times(0);
  EXPECT_CALL(ctx.mock_callbacks, sendLocalReply(Http::Code::BadRequest, "bad request", _, _, _));

  auto status =
      ctx.filter->sendLocalReply(ctx.filter->testDecodingState(), Http::Code::BadRequest,
                                 "bad request", nullptr, Grpc::Status::WellKnownGrpcStatus::Ok, "");
  EXPECT_EQ(CAPIStatus::CAPIOK, status);

  ctx.teardown();
}

// Verify that sendLocalReply posts to the dispatcher when not on the worker thread.
TEST_F(GolangHttpFilterTest, SendLocalReplyPostsOffWorkerThread) {
  MockFilterContext ctx;
  ctx.setup(false);

  EXPECT_CALL(ctx.mock_callbacks.dispatcher_, post(_));

  auto status =
      ctx.filter->sendLocalReply(ctx.filter->testDecodingState(), Http::Code::BadRequest,
                                 "bad request", nullptr, Grpc::Status::WellKnownGrpcStatus::Ok, "");
  EXPECT_EQ(CAPIStatus::CAPIOK, status);

  ctx.teardown();
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
