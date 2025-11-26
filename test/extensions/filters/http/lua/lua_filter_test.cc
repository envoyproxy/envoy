#include <cstdint>
#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/data/core/v3/tlv_metadata.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/lua/lua_filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::AtLeast;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

class LuaHttpFilterTest : public testing::Test {
public:
  LuaHttpFilterTest() {
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

  ~LuaHttpFilterTest() override { filter_->onDestroy(); }

  // Quickly set up a global configuration. In order to avoid extensive modification of existing
  // test cases, the existing configuration methods must be compatible.
  void setup(const std::string& lua_code) {
    envoy::extensions::filters::http::lua::v3::Lua proto_config;
    proto_config.mutable_default_source_code()->set_inline_string(lua_code);
    envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
    setupConfig(proto_config, per_route_proto_config);
    setupFilter();
  }

  void setupConfig(
      const envoy::extensions::filters::http::lua::v3::Lua& proto_config,
      const envoy::extensions::filters::http::lua::v3::LuaPerRoute& per_route_proto_config) {
    // Setup filter config for Lua filter.
    config_ = std::make_shared<FilterConfig>(proto_config, tls_, cluster_manager_, api_,
                                             *stats_store_.rootScope(), "test.");
    // Setup per route config for Lua filter.
    per_route_config_ =
        std::make_shared<FilterConfigPerRoute>(per_route_proto_config, server_factory_context_);
  }

  void setupFilter() {
    Event::SimulatedTimeSystem test_time;
    test_time.setSystemTime(std::chrono::microseconds(1583879145572237));

    filter_ = std::make_unique<Filter>(config_, test_time.timeSystem());
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupSecureConnection(const bool secure) {
    ssl_ = std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>();
    EXPECT_CALL(decoder_callbacks_, connection())
        .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
    EXPECT_CALL(Const(connection_), ssl()).WillOnce(Return(secure ? ssl_ : nullptr));
  }

  void setupMetadata(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, metadata_);
    ON_CALL(*decoder_callbacks_.route_, metadata()).WillByDefault(testing::ReturnRef(metadata_));
  }

  void setupVirtualHostMetadata(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, virtual_host_metadata_);

    auto virtual_host = std::make_shared<NiceMock<Router::MockVirtualHost>>();
    stream_info_.virtual_host_ = virtual_host;

    ON_CALL(*virtual_host, metadata()).WillByDefault(ReturnRef(virtual_host_metadata_));

    ON_CALL(decoder_callbacks_.stream_info_, virtualHost()).WillByDefault(ReturnRef(virtual_host));
    ON_CALL(encoder_callbacks_.stream_info_, virtualHost()).WillByDefault(ReturnRef(virtual_host));

    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
    EXPECT_CALL(encoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));

    const std::string filter_name = "lua-filter-config-name";
    ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(Return(filter_name));
    ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(Return(filter_name));

    EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  }

  void setupRouteMetadata(const std::string& yaml) {
    auto route = std::make_shared<NiceMock<Router::MockRoute>>();
    TestUtility::loadFromYaml(yaml, route->metadata_);

    ON_CALL(stream_info_, route()).WillByDefault(Return(route));

    EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
    EXPECT_CALL(encoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));

    const std::string filter_name = "lua-filter-config-name";
    ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(Return(filter_name));
    ON_CALL(encoder_callbacks_, filterConfigName()).WillByDefault(Return(filter_name));

    EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Api::MockApi> api_;
  Upstream::MockClusterManager cluster_manager_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<FilterConfigPerRoute> per_route_config_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  envoy::config::core::v3::Metadata metadata_;
  envoy::config::core::v3::Metadata virtual_host_metadata_;
  std::shared_ptr<NiceMock<Envoy::Ssl::MockConnectionInfo>> ssl_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  Tracing::MockSpan child_span_;
  Stats::TestUtil::TestStore stats_store_;

  const std::string HEADER_ONLY_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))
    end
  )EOF"};

  const std::string BODY_CHUNK_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))

      for chunk in request_handle:bodyChunks() do
        request_handle:logTrace(chunk:length())
      end

      request_handle:logTrace("done")
    end
  )EOF"};

  const std::string TRAILERS_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))

      for chunk in request_handle:bodyChunks() do
        request_handle:logTrace(chunk:length())
      end

      local trailers = request_handle:trailers()
      if trailers ~= nil then
        request_handle:logTrace(trailers:get("foo"))
      else
        request_handle:logTrace("no trailers")
      end
    end
  )EOF"};

  const std::string TRAILERS_NO_BODY_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))

      if request_handle:trailers() ~= nil then
        request_handle:logTrace(request_handle:trailers():get("foo"))
      else
        request_handle:logTrace("no trailers")
      end
    end
  )EOF"};

  const std::string BODY_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))

      if request_handle:body() ~= nil then
        request_handle:logTrace(request_handle:body():length())
      else
        request_handle:logTrace("no body")
      end
    end
  )EOF"};

  const std::string BODY_TRAILERS_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))

      if request_handle:body() ~= nil then
        request_handle:logTrace(request_handle:body():length())
      else
        request_handle:logTrace("no body")
      end

      if request_handle:trailers() ~= nil then
        request_handle:logTrace(request_handle:trailers():get("foo"))
      else
        request_handle:logTrace("no trailers")
      end
    end
  )EOF"};

  const std::string ADD_HEADERS_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:headers():add("hello", "world")
    end
  )EOF"};

  const std::string REQUEST_RESPONSE_RUNTIME_ERROR_SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      hello:world() -- error
    end

    function envoy_on_response(response_handle)
      bye:world() -- error
    end
  )EOF"};
};

TEST(NoopCallbacksTest, NoopCallbacksTest) {
  NoopCallbacks noop_callbacks;

  NiceMock<Envoy::Http::MockAsyncClient> async_client;
  NiceMock<Envoy::Http::MockAsyncClientRequest> request(&async_client);
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"foo", "bar"}});
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl());
  Tracing::MockSpan span;

  noop_callbacks.onBeforeFinalizeUpstreamSpan(span, response_headers.get());
  noop_callbacks.onFailure(request, {});
  noop_callbacks.onSuccess(request, std::move(response));
}

// Bad code in initial config.
TEST(LuaHttpFilterConfigTest, BadCode) {
  const std::string SCRIPT{R"EOF(
    bad
  )EOF"};

  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Api::MockApi> api;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(SCRIPT);

  EXPECT_THROW_WITH_MESSAGE(
      FilterConfig(proto_config, tls, cluster_manager, api, *stats_store.rootScope(), "lua"),
      Filters::Common::Lua::LuaException,
      "script load error: [string \"...\"]:3: '=' expected near '<eof>'");
}

// Script touching headers only, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestHeadersOnly) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script touching headers only, request that has body.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBody) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script touching headers only, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBodyTrailers) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for body chunks, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestHeadersOnly) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "/"},
                                 {"trace", "done"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
}

// Script asking for body chunks, request that has body.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBody) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({
          {"trace", "5"},
          {"trace", "done"},
      }),
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for body chunks, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBodyTrailers) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("trace", "5", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  });

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS("trace", "done", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "/"},
                                 {"trace", "no trailers"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBody) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({
          {"trace", "5"},
          {"trace", "no trailers"},
      }),
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("trace", "5", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  });

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS("trace", "bar", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers without body, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "/"},
                                 {"trace", "no trailers"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers without body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBody) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("trace", "no trailers", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for trailers without body, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS("trace", "bar", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestHeadersOnly) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "/"},
                                 {"trace", "no body"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBody) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("trace", "5", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body, request that has a body in multiple frames.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFrames) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_LOG_CONTAINS("trace", "10", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Scripting asking for synchronous body, request that has a body in multiple frames follows by
// trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFramesTrailers) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data2, false));
  decoder_callbacks_.addDecodedData(data2, false);

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS("trace", "10", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body and trailers, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestHeadersOnly) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "/"},
                                 {"trace", "no body"},
                                 {"trace", "no trailers"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body and trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBody) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({
          {"trace", "5"},
          {"trace", "no trailers"},
      }),
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script asking for synchronous body and trailers, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBodyTrailers) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "5"},
                                 {"trace", "bar"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterTrailersStatus::Continue,
                                         filter_->decodeTrailers(request_trailers));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Store a body chunk and reference it outside the loop.
TEST_F(LuaHttpFilterTest, BodyChunkOutsideOfLoop) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      for chunk in request_handle:bodyChunks() do
        if previous_chunk == nil then
          previous_chunk = chunk
        else
          previous_chunk:length()
        end
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data1, false));

  Buffer::OwnedImpl data2("world");
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:7: object used outside of proper scope", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, false));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script that should not be run.
TEST_F(LuaHttpFilterTest, ScriptRandomRequestBodyTrailers) {
  const std::string SCRIPT{R"EOF(
    function some_random_function()
      print("don't run me")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(0, stats_store_.counter("test.lua.executions").value());
}

// Script that has an error during headers processing.
TEST_F(LuaHttpFilterTest, ScriptErrorHeadersRequestBodyTrailers) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local foo = nil
      foo["bar"] = "baz"
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:4: attempt to index local 'foo' (a nil value)", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script that tries to store a local variable to a global and then use it.
TEST_F(LuaHttpFilterTest, ThreadEnvironments) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if global_request_handle == nil then
        global_request_handle = request_handle
      else
        global_request_handle:logTrace("should not work")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Event::SimulatedTimeSystem test_time;
  Filter filter2(config_, test_time.timeSystem());
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:6: object used outside of proper scope",
                      { filter2.decodeHeaders(request_headers, true); });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(2, stats_store_.counter("test.lua.executions").value());
}

// Script that yields on its own.
TEST_F(LuaHttpFilterTest, UnexpectedYield) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      coroutine.yield()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "script performed an unexpected yield", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script that has an error during a callback from C into Lua.
TEST_F(LuaHttpFilterTest, ErrorDuringCallback) {
  const std::string SCRIPT(R"EOF(
    function envoy_on_request(request_handle)
      for key, value in pairs(request_handle:headers()) do
        local foo = nil
        foo["bar"] = "baz"
      end
    end
  )EOF");

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:5: attempt to index local 'foo' (a nil value)", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Use of header iterator across yield.
TEST_F(LuaHttpFilterTest, HeadersIteratorAcrossYield) {
  const std::string SCRIPT(R"EOF(
    function envoy_on_request(request_handle)
      local headers_it = pairs(request_handle:headers())
      request_handle:body()
      headers_it()
    end
  )EOF");

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:5: object used outside of proper scope", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Combo request and response script.
TEST_F(LuaHttpFilterTest, RequestAndResponse) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:headers():get(":path"))
      request_handle:headers():add("foo", "bar")

      for chunk in request_handle:bodyChunks() do
        request_handle:logTrace(chunk:length())
      end

      request_handle:logTrace(request_handle:trailers():get("foo"))
    end

    function envoy_on_response(response_handle)
      response_handle:logTrace(response_handle:headers():get(":status"))
      response_handle:headers():add("foo", "bar")

      for chunk in response_handle:bodyChunks() do
        response_handle:logTrace(chunk:length())
      end

      response_handle:logTrace(response_handle:trailers():get("hello"))
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "/", {
    EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS("trace", "5", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  });

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS("trace", "bar", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  });

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  // No lua hooks for 100-continue
  EXPECT_LOG_NOT_CONTAINS("trace", "100", {
    EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(continue_headers));
  });

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "200", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  });

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_LOG_CONTAINS("trace", "10", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, false));
  });

  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_LOG_CONTAINS("trace", "world", {
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(2, stats_store_.counter("test.lua.executions").value());
}

// Response synchronous body.
TEST_F(LuaHttpFilterTest, ResponseSynchronousBody) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      response_handle:logTrace(response_handle:headers():get(":status"))
      response_handle:logTrace(response_handle:body():length())
      if response_handle:trailers() == nil then
        response_handle:logTrace("no trailers")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "200", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers, false));
  });

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({
          {"trace", "10"},
          {"trace", "no trailers"},
      }),
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Basic HTTP request flow.
TEST_F(LuaHttpFilterTest, HttpCall) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
          ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
        },
        "hello world",
        5000)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      request_handle:logTrace(string.len(body))
      request_handle:logTrace(body)
      request_handle:logTrace(string.byte(body, 5))
      request_handle:logTrace(string.sub(body, 6, 8))
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":method", "POST"},
                {":path", "/"},
                {":authority", "foo"},

                {"set-cookie", "flavor=chocolate; Path=/"},
                {"set-cookie", "variant=chewy; Path=/"},
                {"content-length", "11"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            // The parent span always be set for lua http call.
            EXPECT_NE(options.parent_span_, nullptr);

            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  const char response[8] = {'r', 'e', 's', 'p', '\0', 'n', 's', 'e'};
  response_message->body().add(response, 8);
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 200"},
                                 {"trace", "8"},
                                 {"trace", std::string("resp\0nse", 8)},
                                 {"trace", "0"},
                                 {"trace", "nse"},
                             }),
                             {
                               callbacks->onBeforeFinalizeUpstreamSpan(
                                   child_span_, &response_message->headers());
                               callbacks->onSuccess(request, std::move(response_message));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP request flow with multiple header values for same header name.
TEST_F(LuaHttpFilterTest, HttpCallWithRepeatedHeaders) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
        },
        "hello world",
        {
          ["return_duplicate_headers"] = true
        })
      for key, value in pairs(headers) do
        if type(value) == "table" then
          request_handle:logTrace(key .. " " .. table.concat(value, ","))
        else
          request_handle:logTrace(key .. " " .. value)
        end
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(
      new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
          new Http::TestResponseHeaderMapImpl{{"key", "value"}, {"key", "second_value"}}}));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS("trace", "key value,second_value", {
    callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
    callbacks->onSuccess(request, std::move(response_message));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Basic HTTP request flow. Asynchronous flag set to false.
TEST_F(LuaHttpFilterTest, HttpCallAsyncFalse) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
          ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
        },
        "hello world",
        5000,
        false)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      request_handle:logTrace(body)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"},
                {":method", "POST"},
                {":authority", "foo"},
                {"set-cookie", "flavor=chocolate; Path=/"},
                {"set-cookie", "variant=chewy; Path=/"},
                {"content-length", "11"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response_message->body().add("response");
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 200"},
                                 {"trace", "response"},
                             }),
                             { callbacks->onSuccess(request, std::move(response_message)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Basic asynchronous, fire-and-forget HTTP request flow.
TEST_F(LuaHttpFilterTest, HttpCallAsynchronous) {
  const std::string SCRIPT{R"EOF(
        function envoy_on_request(request_handle)
          local headers, body = request_handle:httpCall(
            "cluster",
            {
              [":method"] = "POST",
              [":path"] = "/",
              [":authority"] = "foo",
              ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
            },
            "hello world",
            5000,
            true)
        end
      )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"},
                {":method", "POST"},
                {":authority", "foo"},
                {"set-cookie", "flavor=chocolate; Path=/"},
                {"set-cookie", "variant=chewy; Path=/"},
                {"content-length", "11"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Basic asynchronous, fire-and-forget HTTP request flow.
TEST_F(LuaHttpFilterTest, HttpCallAsynchronousInOptions) {
  const std::string SCRIPT{R"EOF(
        function envoy_on_request(request_handle)
          local headers, body = request_handle:httpCall(
            "cluster",
            {
              [":method"] = "POST",
              [":path"] = "/",
              [":authority"] = "foo",
              ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
            },
            "hello world",
            {
              ["asynchronous"] = true
            })
        end
      )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"},
                {":method", "POST"},
                {":authority", "foo"},
                {"set-cookie", "flavor=chocolate; Path=/"},
                {"set-cookie", "variant=chewy; Path=/"},
                {"content-length", "11"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Double HTTP call. Responses before request body.
TEST_F(LuaHttpFilterTest, DoubleHttpCall) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        "hello world",
        5000)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      request_handle:logTrace(body)

      headers, body = request_handle:httpCall(
        "cluster2",
        {
          [":method"] = "GET",
          [":path"] = "/bar",
          [":authority"] = "foo"
        },
        nil,
        0)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      if body == nil then
        request_handle:logTrace("no body")
      end
    end
  )EOF"};

  InSequence s;
  cluster_manager_.initializeThreadLocalClusters({"cluster", "cluster2"});
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{{":path", "/"},
                                                                  {":method", "POST"},
                                                                  {":authority", "foo"},
                                                                  {"content-length", "11"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response_message->body().add("response");
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster2")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/bar"}, {":method", "GET"}, {":authority", "foo"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 200"},
                                 {"trace", "response"},
                             }),
                             { callbacks->onSuccess(request, std::move(response_message)); });

  response_message = std::make_unique<Http::ResponseMessageImpl>(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "403"}}});
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 403"},
                                 {"trace", "no body"},
                             }),
                             {
                               callbacks->onBeforeFinalizeUpstreamSpan(
                                   child_span_, &response_message->headers());
                               callbacks->onSuccess(request, std::move(response_message));
                             });

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Basic HTTP request flow with no body.
TEST_F(LuaHttpFilterTest, HttpCallNoBody) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      if body == nil then
        request_handle:logTrace("no body")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            // We are actively deferring to the parent span's sampled state.
            EXPECT_FALSE(options.sampled_.has_value());
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"}, {":method", "GET"}, {":authority", "foo"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 200"},
                                 {"trace", "no body"},
                             }),
                             { callbacks->onSuccess(request, std::move(response_message)); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP call followed by immediate response.
TEST_F(LuaHttpFilterTest, HttpCallImmediateResponse) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)
      request_handle:respond(
        {
          [":status"] = "403",
          ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
        },
        nil)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP call with script error after resume.
TEST_F(LuaHttpFilterTest, HttpCallErrorAfterResumeSuccess) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)

        local foo = nil
        foo["bar"] = "baz"
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:14: attempt to index local 'foo' (a nil value)",
                      { callbacks->onSuccess(request, std::move(response_message)); });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP call failure.
TEST_F(LuaHttpFilterTest, HttpCallFailure) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)

        for key, value in pairs(headers) do
          request_handle:logTrace(key .. " " .. value)
        end
        request_handle:logTrace(body)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  EXPECT_LOG_CONTAINS_ALL_OF(
      Envoy::ExpectedLogMessages({
          {"trace", ":status 503"},
          {"trace", "upstream failure"},
      }),
      { callbacks->onFailure(request, Http::AsyncClient::FailureReason::Reset); });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP call reset.
TEST_F(LuaHttpFilterTest, HttpCallReset) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)

        request_handle:logTrace("not run")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(request, cancel());
  filter_->onDestroy();
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP call immediate failure.
TEST_F(LuaHttpFilterTest, HttpCallImmediateFailure) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "GET",
          [":path"] = "/",
          [":authority"] = "foo"
        },
        nil,
        5000)

        for key, value in pairs(headers) do
          request_handle:logTrace(key .. " " .. value)
        end
        request_handle:logTrace(body)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            cb.onFailure(request, Http::AsyncClient::FailureReason::Reset);
            // Intentionally return nullptr (instead of request handle) to trigger a particular
            // code path.
            return nullptr;
          }));

  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", ":status 503"},
                                 {"trace", "upstream failure"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Invalid HTTP call timeout.
TEST_F(LuaHttpFilterTest, HttpCallInvalidTimeout) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {},
        nil,
        -1)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: http call timeout must be >= 0", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Invalid HTTP call timeout in options.
TEST_F(LuaHttpFilterTest, HttpCallInvalidTimeoutInOptions) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {},
        nil,
        {
          ["timeout_ms"] = -1
        })
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: http call timeout must be >= 0", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Invalid HTTP call cluster.
TEST_F(LuaHttpFilterTest, HttpCallInvalidCluster) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {},
        nil,
        5000)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster"))).WillOnce(Return(nullptr));
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: http call cluster invalid. Must be configured",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, false));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP request flow with timeout, sampled and send_xff flag in options.
TEST_F(LuaHttpFilterTest, HttpCallWithTimeoutAndSampledInOptions) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
        },
        "hello world",
        {
          ["timeout_ms"] = 5000,
          ["trace_sampled"] = false,
          ["send_xff"] = false,
        })
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
              const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_EQ(options.timeout->count(), 5000);
            EXPECT_EQ(options.sampled_.value(), false);
            EXPECT_EQ(options.send_xff, false);
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// HTTP request flow with timeout and invalid flag in options.
TEST_F(LuaHttpFilterTest, HttpCallWithInvalidOption) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
        },
        "hello world",
        {
          ["timeout_ms"] = 5000,
          ["invalid_option"] = false,
        })
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:3: \"invalid_option\" is not valid key for httpCall() options", {
        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                  filter_->decodeHeaders(request_headers, false));
      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Invalid HTTP call headers.
TEST_F(LuaHttpFilterTest, HttpCallInvalidHeaders) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local headers, body = request_handle:httpCall(
        "cluster",
        {},
        nil,
        5000)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_LOG_CONTAINS("error",
                      "[string \"...\"]:3: http call headers must include "
                      "':path', ':method', and ':authority'",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, false));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Invalid HTTP call asynchronous flag value.
TEST_F(LuaHttpFilterTest, HttpCallAsyncInvalidAsynchronousFlag) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:httpCall(
        "cluster",
        {
          [":method"] = "POST",
          [":path"] = "/",
          [":authority"] = "foo",
          ["set-cookie"] = { "flavor=chocolate; Path=/", "variant=chewy; Path=/" }
        },
        "hello world",
        5000,
        potato)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error",
                      "[string \"...\"]:3: http call asynchronous flag "
                      "must be 'true', 'false', or empty",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, false));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Respond right away.
// This is also a regression test for https://github.com/envoyproxy/envoy/issues/3570 which runs
// the request flow 2000 times and does a GC at the end to make sure we don't leak memory.
TEST_F(LuaHttpFilterTest, ImmediateResponse) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:respond(
        {[":status"] = "503"},
        "nope")

      -- Should not run
      local foo = nil
      foo["bar"] = "baz"
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Perform a GC and snap bytes currently used by the runtime.
  auto script_config = config_->perLuaCodeSetup();
  script_config->runtimeGC();
  const uint64_t mem_use_at_start = script_config->runtimeBytesUsed();

  uint64_t num_loops = 2000;
#if defined(__has_feature) && (__has_feature(thread_sanitizer))
  // per https://github.com/envoyproxy/envoy/issues/7374 this test is causing
  // problems on tsan
  num_loops = 200;
#endif

  for (uint64_t i = 0; i < num_loops; i++) {
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    Http::TestResponseHeaderMapImpl immediate_response_headers;
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
        .WillOnce(Invoke([&immediate_response_headers](
                             Http::Code code, absl::string_view body,
                             std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                             const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                             absl::string_view details) {
          EXPECT_EQ(Http::Code::ServiceUnavailable, code);
          EXPECT_EQ("nope", body);
          EXPECT_EQ(grpc_status, absl::nullopt);
          EXPECT_EQ(details, "lua_response");
          modify_headers(immediate_response_headers);
        }));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers, false));
    filter_->onDestroy();
    setupFilter();
  }

  // Perform GC and compare bytes currently used by the runtime to the original value.
  // NOTE: This value is not the same as the original value for reasons that I do not fully
  //       understand. Depending on the number of requests tested, it increases incrementally, but
  //       then goes down again at a certain point. There must be some type of interpreter caching
  //       going on because I'm pretty certain this is not another leak. Because of this, we need
  //       to do a soft comparison here. In my own testing, without a fix for #3570, the memory
  //       usage after is at least 20x higher after 2000 iterations so we just check to see if it's
  //       within 2x.
  script_config->runtimeGC();
  EXPECT_TRUE(script_config->runtimeBytesUsed() < mem_use_at_start * 2);
}

TEST_F(LuaHttpFilterTest, ImmediateResponseWithSendLocalReply) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:respond(
        {[":status"] = "503",
         ["fake"] = "fakeValue"},
        "nope")

      -- Should not run
      local foo = nil
      foo["bar"] = "baz"
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Http::TestResponseHeaderMapImpl immediate_response_headers;
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&immediate_response_headers](
                           Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::ServiceUnavailable, code);
        EXPECT_EQ("nope", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "lua_response");
        modify_headers(immediate_response_headers);
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_TRUE(immediate_response_headers.has("fake"));
  EXPECT_EQ(immediate_response_headers.get_("fake"), "fakeValue");
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Respond with bad status.
TEST_F(LuaHttpFilterTest, ImmediateResponseBadStatus) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:respond(
        {[":status"] = "100"},
        "nope")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: :status must be between 200-599", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Respond after headers have been continued.
TEST_F(LuaHttpFilterTest, RespondAfterHeadersContinued) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      for chunk in request_handle:bodyChunks() do
        request_handle:respond(
          {[":status"] = "100"},
          "nope")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:4: respond() cannot be called if headers have been continued",
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false)); });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Respond in response path.
TEST_F(LuaHttpFilterTest, RespondInResponsePath) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      response_handle:respond(
        {[":status"] = "200"},
        "nope")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("error",
                      "[string \"...\"]:3: respond not currently supported in the response path", {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->encodeHeaders(response_headers, true));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// bodyChunks() after body continued.
TEST_F(LuaHttpFilterTest, BodyChunksAfterBodyContinued) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:body()
      request_handle:bodyChunks()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:4: cannot call bodyChunks after body processing has begun",
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true)); });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// body() after only waiting for trailers.
TEST_F(LuaHttpFilterTest, BodyAfterTrailers) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:trailers()
      request_handle:body()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:4: cannot call body() after body has been streamed", {
        EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// body() after streaming has started.
TEST_F(LuaHttpFilterTest, BodyAfterStreamingHasStarted) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      for chunk in request_handle:bodyChunks() do
        request_handle:body()
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:4: cannot call body() after body streaming has started",
      { EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false)); });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// script touch metadata():get("key")
TEST_F(LuaHttpFilterTest, GetMetadataFromHandle) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:metadata():get("foo.bar")["name"])
      request_handle:logTrace(request_handle:metadata():get("foo.bar")["prop"])
      request_handle:logTrace(request_handle:metadata():get("baz.bat")["name"])
      request_handle:logTrace(request_handle:metadata():get("baz.bat")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
      envoy.filters.http.lua:
        foo.bar:
          name: foo-xxx
          prop: bar-xxx
        baz.bat:
          name: baz-xxx
          prop: bat-xxx
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupMetadata(METADATA);

  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(Return("lua-filter-config-name"));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, GetMetadataFromHandleWithCanonicalName) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:metadata():get("foo.bar")["name"])
      request_handle:logTrace(request_handle:metadata():get("foo.bar")["prop"])
      request_handle:logTrace(request_handle:metadata():get("baz.bat")["name"])
      request_handle:logTrace(request_handle:metadata():get("baz.bat")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.filters.http.lua:
        foo.bar:
          name: foo-xxx
          prop: bar-xxx
        baz.bat:
          name: baz-xxx
          prop: bat-xxx
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupMetadata(METADATA);

  ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(Return("lua-filter-config-name"));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo-xxx"},
                                 {"trace", "bar-xxx"},
                                 {"trace", "baz-xxx"},
                                 {"trace", "bat-xxx"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// No available metadata on route.
TEST_F(LuaHttpFilterTest, GetMetadataFromHandleNoRoute) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:metadata():get("foo.bar") == nil then
        request_handle:logTrace("ok")
      end
    end
  )EOF"};

  InSequence s;
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "ok", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
}

// No available Lua metadata on route.
TEST_F(LuaHttpFilterTest, GetMetadataFromHandleNoLuaMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:metadata():get("foo.bar") == nil then
        request_handle:logTrace("ok")
      end
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.some_filter:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupMetadata(METADATA);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "ok", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Get the current protocol.
TEST_F(LuaHttpFilterTest, GetCurrentProtocol) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:streamInfo():protocol())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, protocol()).WillOnce(Return(Http::Protocol::Http11));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "HTTP/1.1", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Get the requested server name.
TEST_F(LuaHttpFilterTest, GetRequestedServerName) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:streamInfo():requestedServerName())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  absl::string_view server_name = "foo.example.com";
  stream_info_.downstream_connection_info_provider_->setRequestedServerName(server_name);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "foo.example.com", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify that network connection level streamInfo():dynamicMetadata() could be accessed using LUA.
TEST_F(LuaHttpFilterTest, GetConnectionDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local cx_metadata = request_handle:connectionStreamInfo():dynamicMetadata()
      local filters_count = 0
      for filter_name, _ in pairs(cx_metadata) do
        filters_count = filters_count + 1
      end
      request_handle:logTrace('Filters Count: ' .. filters_count)

      local pp_metadata_entries = cx_metadata:get("envoy.proxy_protocol")
      for key, value in pairs(pp_metadata_entries) do
        request_handle:logTrace('Key: ' .. key .. ', Value: ' .. value)
      end

      local lb_version = cx_metadata:get("envoy.lb")["version"]
      request_handle:logTrace('Key: version, Value: ' .. lb_version)
    end
  )EOF"};

  // Proxy Protocol Filter Metadata
  Protobuf::Value tlv_ea_value;
  tlv_ea_value.set_string_value("vpce-064c279a4001a055f");
  Protobuf::Struct proxy_protocol_metadata;
  proxy_protocol_metadata.mutable_fields()->insert({"tlv_ea", tlv_ea_value});
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.proxy_protocol"] =
      proxy_protocol_metadata;

  // LB Filter Metadata
  Protobuf::Value lb_version_value;
  lb_version_value.set_string_value("v1.0");
  Protobuf::Struct lb_metadata;
  lb_metadata.mutable_fields()->insert({"version", lb_version_value});
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.lb"] = lb_metadata;

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "Filters Count: 2"},
                                 {"trace", "Key: tlv_ea, Value: vpce-064c279a4001a055f"},
                                 {"trace", "Key: version, Value: v1.0"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify that typed metadata on the connection stream info could be accessed using LUA.
TEST_F(LuaHttpFilterTest, GetConnectionTypedMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.filters.listener.proxy_protocol")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
        -- The typed metadata is structured with field keys
        if typed_meta.fields and typed_meta.fields.typed_metadata and typed_meta.fields.typed_metadata.struct_value then
          request_handle:logTrace("Has TLV data: true")
          local tlv_data = typed_meta.fields.typed_metadata.struct_value.fields
          request_handle:logTrace("TLV EA: " .. tlv_data.tlv_ea.string_value)
          request_handle:logTrace("TLV PP2 type: " .. tlv_data.pp2_type.string_value)
        else
          request_handle:logTrace("Has TLV data: false")
        end
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a simple Struct for testing typed metadata
  Protobuf::Struct main_struct;

  // Create a nested struct for typed_metadata
  Protobuf::Struct typed_metadata_struct;
  (*typed_metadata_struct.mutable_fields())["tlv_ea"].set_string_value("vpce-1234567890abcdef");
  (*typed_metadata_struct.mutable_fields())["pp2_type"].set_string_value("PROXY");

  // Add the typed_metadata to the main struct
  auto* typed_meta_value = &(*main_struct.mutable_fields())["typed_metadata"];
  typed_meta_value->mutable_struct_value()->MergeFrom(typed_metadata_struct);

  Protobuf::Any typed_config;
  typed_config.PackFrom(main_struct);

  // Add the typed metadata to the stream info
  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.listener.proxy_protocol", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "Has typed metadata: true"},
                                 {"trace", "Has TLV data: true"},
                                 {"trace", "TLV EA: vpce-1234567890abcdef"},
                                 {"trace", "TLV PP2 type: PROXY"},
                             }),
                             EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                       filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify that complex typed metadata could be accessed and traversed using LUA.
TEST_F(LuaHttpFilterTest, GetConnectionTypedMetadataComplex) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.filters.listener.proxy_protocol")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")

        -- The typed metadata is structured with a fields key
        if typed_meta.fields then
          -- Access SSL info (nested struct)
          if typed_meta.fields.ssl_info and typed_meta.fields.ssl_info.struct_value then
            local ssl_info = typed_meta.fields.ssl_info.struct_value.fields
            request_handle:logTrace("SSL version: " .. ssl_info.version.string_value)
            request_handle:logTrace("SSL cipher: " .. ssl_info.cipher.string_value)
          end

          -- Access addresses (array)
          if typed_meta.fields.addresses and typed_meta.fields.addresses.list_value then
            local addresses = typed_meta.fields.addresses.list_value.values
            for i, addr in ipairs(addresses) do
              request_handle:logTrace("Address " .. i .. ": " .. addr.string_value)
            end
          end
        end
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a complex Struct for testing
  Protobuf::Struct main_struct;

  // Add simple key/value pairs
  (*main_struct.mutable_fields())["tlv_ea"].set_string_value("vpce-1234567890abcdef");
  (*main_struct.mutable_fields())["pp2_type"].set_string_value("PROXY");

  // Create a nested struct for SSL info
  Protobuf::Struct ssl_info;
  (*ssl_info.mutable_fields())["version"].set_string_value("TLSv1.3");
  (*ssl_info.mutable_fields())["cipher"].set_string_value("ECDHE-RSA-AES128-GCM-SHA256");

  // Add the SSL info to the main struct
  auto* ssl_value = &(*main_struct.mutable_fields())["ssl_info"];
  ssl_value->mutable_struct_value()->MergeFrom(ssl_info);

  // Create an array of addresses
  Protobuf::ListValue addresses;
  addresses.add_values()->set_string_value("192.168.1.1");
  addresses.add_values()->set_string_value("10.0.0.1");
  addresses.add_values()->set_string_value("172.16.0.1");

  // Add the addresses to the main struct
  auto* addresses_value = &(*main_struct.mutable_fields())["addresses"];
  addresses_value->mutable_list_value()->MergeFrom(addresses);

  Protobuf::Any typed_config;
  typed_config.PackFrom(main_struct);

  // Add the typed metadata to the stream info
  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.listener.proxy_protocol", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "Has typed metadata: true"},
                                 {"trace", "SSL version: TLSv1.3"},
                                 {"trace", "SSL cipher: ECDHE-RSA-AES128-GCM-SHA256"},
                                 {"trace", "Address 1: 192.168.1.1"},
                                 {"trace", "Address 2: 10.0.0.1"},
                                 {"trace", "Address 3: 172.16.0.1"},
                             }),
                             EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                       filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify behavior when typed metadata is not found for a filter.
TEST_F(LuaHttpFilterTest, GetConnectionTypedMetadataNotFound) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("unknown.filter")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS("trace", "Has typed metadata: false",
                      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify behavior when the type URL in typed metadata cannot be found in the Protobuf descriptor
// pool.
TEST_F(LuaHttpFilterTest, GetConnectionTypedMetadataInvalidType) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.test.metadata")
      if typed_meta["value"] == nil then
        request_handle:logTrace("metadata value is nil")
      else
        request_handle:logTrace(typed_meta["value"])
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Pack an invalid/unknown message type
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/unknown.type");
  typed_config.set_value("invalid data");

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.test.metadata", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify behavior when the data in typed metadata cannot be unpacked.
TEST_F(LuaHttpFilterTest, GetConnectionTypedMetadataUnpackFailure) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:connectionStreamInfo():dynamicTypedMetadata("envoy.filters.listener.proxy_protocol")
      if typed_meta["typed_metadata"] ~= nil then
        request_handle:logTrace("key: " .. typed_meta["typed_metadata"]["tlv_ea"])
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Pack invalid data that will fail to unpack
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/envoy.data.core.v3.TlvsMetadata");
  typed_config.set_value("invalid protobuf data");

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.listener.proxy_protocol", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, connection())
      .WillOnce(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(Const(connection_), streamInfo()).WillOnce(ReturnRef(stream_info_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Verify that binary values could also be extracted from dynamicMetadata() in LUA filter.
TEST_F(LuaHttpFilterTest, GetDynamicMetadataBinaryData) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local metadata = request_handle:streamInfo():dynamicMetadata():get("envoy.pp")
      local bin_data = metadata["bin_data"]
      local data_length = string.len(metadata["bin_data"])
      local hex_table = { }
      for idx = 1, data_length do
        hex_table[#hex_table + 1] = string.format("\\x%02x", string.byte(bin_data, idx))
      end
      request_handle:logTrace('Hex Data: ' .. table.concat(hex_table, ''))
    end
  )EOF"};

  Protobuf::Value metadata_value;
  constexpr uint8_t buffer[] = {'h', 'e', 0x00, 'l', 'l', 'o'};
  metadata_value.set_string_value(reinterpret_cast<char const*>(buffer), sizeof(buffer));
  Protobuf::Struct metadata;
  metadata.mutable_fields()->insert({"bin_data", metadata_value});
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.pp"] = metadata;

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  // Hex values for the buffer data
  EXPECT_LOG_CONTAINS("trace", "Hex Data: \\x68\\x65\\x00\\x6c\\x6c\\x6f", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Set and get stream info dynamic metadata.
TEST_F(LuaHttpFilterTest, SetGetDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:streamInfo():dynamicMetadata():set("envoy.lb", "foo", "bar")
      request_handle:streamInfo():dynamicMetadata():set("envoy.lb", "complex", {x="abcd", y=1234})
      request_handle:logTrace(request_handle:streamInfo():dynamicMetadata():get("envoy.lb")["foo"])
      request_handle:logTrace(request_handle:streamInfo():dynamicMetadata():get("envoy.lb")["complex"].x)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Event::SimulatedTimeSystem test_time;
  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "bar"},
                                 {"trace", "abcd"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_EQ("bar", stream_info.dynamicMetadata()
                       .filter_metadata()
                       .at("envoy.lb")
                       .fields()
                       .at("foo")
                       .string_value());

  const Protobuf::Struct& meta_complex = stream_info.dynamicMetadata()
                                             .filter_metadata()
                                             .at("envoy.lb")
                                             .fields()
                                             .at("complex")
                                             .struct_value();
  EXPECT_EQ("abcd", meta_complex.fields().at("x").string_value());
  EXPECT_EQ(1234.0, meta_complex.fields().at("y").number_value());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Check the connection.
TEST_F(LuaHttpFilterTest, CheckConnection) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:connection():ssl() == nil then
        request_handle:logTrace("plain")
      else
        request_handle:logTrace("secure")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  setupSecureConnection(false);
  EXPECT_LOG_CONTAINS("trace", "plain", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  setupSecureConnection(true);
  EXPECT_LOG_CONTAINS("trace", "secure", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
}

// Inspect stream info downstream SSL connection.
TEST_F(LuaHttpFilterTest, InspectStreamInfoDowstreamSslConnection) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:streamInfo():downstreamSslConnection() == nil then
      else
        if request_handle:streamInfo():downstreamSslConnection():peerCertificatePresented() then
          request_handle:logTrace("peerCertificatePresented")
        end

        if request_handle:streamInfo():downstreamSslConnection():peerCertificateValidated() then
          request_handle:logTrace("peerCertificateValidated")
        end

        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():uriSanPeerCertificate(), ","))
        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():uriSanLocalCertificate(), ","))
        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():dnsSansPeerCertificate(), ","))
        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():dnsSansLocalCertificate(), ","))

        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():oidsPeerCertificate(), ","))
        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():oidsLocalCertificate(), ","))

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():ciphersuiteId())

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():validFromPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():expirationPeerCertificate())

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():subjectLocalCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():sha256PeerCertificateDigest())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():serialNumberPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():issuerPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():subjectPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():parsedSubjectPeerCertificate():commonName())
        request_handle:logTrace(table.concat(request_handle:streamInfo():downstreamSslConnection():parsedSubjectPeerCertificate():organizationName(), ","))
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():ciphersuiteString())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():tlsVersion())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():urlEncodedPemEncodedPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():urlEncodedPemEncodedPeerCertificateChain())

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():sessionId())
      end
    end
  )EOF"};

  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  const auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  stream_info_.downstream_connection_info_provider_->setSslConnection(connection_info);

  EXPECT_CALL(*connection_info, peerCertificatePresented()).WillOnce(Return(true));

  EXPECT_CALL(*connection_info, peerCertificateValidated()).WillOnce(Return(true));

  const std::vector<std::string> peer_uri_sans{"peer-uri-sans-1", "peer-uri-sans-2"};
  EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillOnce(Return(peer_uri_sans));

  const std::vector<std::string> local_uri_sans{"local-uri-sans-1", "local-uri-sans-2"};
  EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));

  const std::vector<std::string> peer_dns_sans{"peer-dns-sans-1", "peer-dns-sans-2"};
  EXPECT_CALL(*connection_info, dnsSansPeerCertificate()).WillOnce(Return(peer_dns_sans));

  const std::vector<std::string> local_dns_sans{"local-dns-sans-1", "local-dns-sans-2"};
  EXPECT_CALL(*connection_info, dnsSansLocalCertificate()).WillOnce(Return(local_dns_sans));

  const std::vector<std::string> peer_oids{"2.5.29.14", "1.2.840.113635.100"};
  EXPECT_CALL(*connection_info, oidsPeerCertificate()).WillOnce(Return(peer_oids));

  const std::vector<std::string> local_oids{"2.5.29.14", "2.5.29.15", "2.5.29.19"};
  EXPECT_CALL(*connection_info, oidsLocalCertificate()).WillOnce(Return(local_oids));

  const std::string subject_local = "subject-local";
  EXPECT_CALL(*connection_info, subjectLocalCertificate()).WillOnce(ReturnRef(subject_local));

  const uint64_t cipher_suite_id = 0x0707;
  EXPECT_CALL(*connection_info, ciphersuiteId()).WillRepeatedly(Return(cipher_suite_id));

  const SystemTime validity(std::chrono::seconds(1522796777));
  EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(validity));

  const SystemTime expiry(std::chrono::seconds(1522796776));
  EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(expiry));

  const std::string peer_cert_digest = "peer-cert-digest";
  EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
      .WillOnce(ReturnRef(peer_cert_digest));

  const std::string peer_cert_serial_number = "peer-cert-serial-number";
  EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
      .WillOnce(ReturnRef(peer_cert_serial_number));

  const std::string peer_cert_issuer = "peer-cert-issuer";
  EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillOnce(ReturnRef(peer_cert_issuer));

  const std::string peer_cert_subject = "peer-cert-subject";
  EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillOnce(ReturnRef(peer_cert_subject));

  Ssl::ParsedX509Name parsed_subject;
  parsed_subject.commonName_ = "Test CN";
  parsed_subject.organizationName_.push_back("Test O1");
  parsed_subject.organizationName_.push_back("Test O2");
  Ssl::ParsedX509NameOptConstRef const_parsed_subject(parsed_subject);
  EXPECT_CALL(*connection_info, parsedSubjectPeerCertificate())
      .WillRepeatedly(Return(const_parsed_subject));

  const std::string cipher_suite = "cipher-suite";
  EXPECT_CALL(*connection_info, ciphersuiteString()).WillOnce(Return(cipher_suite));

  const std::string tls_version = "tls-version";
  EXPECT_CALL(*connection_info, tlsVersion()).WillOnce(ReturnRef(tls_version));

  const std::string peer_cert = "peer-cert";
  EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillOnce(ReturnRef(peer_cert));

  const std::string peer_cert_chain = "peer-cert-chain";
  EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificateChain())
      .WillOnce(ReturnRef(peer_cert_chain));

  const std::string id = "12345";
  EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(id));

  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "peerCertificatePresented"},
                                 {"trace", "peerCertificateValidated"},
                                 {"trace", "peer-uri-sans-1,peer-uri-sans-2"},
                                 {"trace", "local-uri-sans-1,local-uri-sans-2"},
                                 {"trace", "peer-dns-sans-1,peer-dns-sans-2"},
                                 {"trace", "local-dns-sans-1,local-dns-sans-2"},
                                 {"trace", "2.5.29.14,1.2.840.113635.100"},
                                 {"trace", "2.5.29.14,2.5.29.15,2.5.29.19"},
                                 {"trace", subject_local},
                                 {"trace", "0x0707"},
                                 {"trace", "1522796777"},
                                 {"trace", "1522796776"},
                                 {"trace", peer_cert_digest},
                                 {"trace", peer_cert_serial_number},
                                 {"trace", peer_cert_issuer},
                                 {"trace", peer_cert_subject},
                                 {"trace", "Test CN"},
                                 {"trace", "Test O1,Test O2"},
                                 {"trace", cipher_suite},
                                 {"trace", tls_version},
                                 {"trace", peer_cert},
                                 {"trace", peer_cert_chain},
                                 {"trace", id},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
}

// Inspect stream info downstream SSL connection in a plain connection.
TEST_F(LuaHttpFilterTest, InspectStreamInfoDowstreamSslConnectionOnPlainConnection) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:streamInfo():downstreamSslConnection() == nil then
        request_handle:logTrace("downstreamSslConnection is nil")
      end
    end
  )EOF"};

  setup(SCRIPT);

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  stream_info_.downstream_connection_info_provider_->setSslConnection(nullptr);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "downstreamSslConnection is nil", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
}

// Should survive from multiple streamInfo():downstreamSslConnection() calls.
// This is a regression test for #14091.
TEST_F(LuaHttpFilterTest, SurviveMultipleDownstreamSslConnectionCalls) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:streamInfo():downstreamSslConnection() ~= nil then
         request_handle:logTrace("downstreamSslConnection is present")
      end
    end
  )EOF"};

  setup(SCRIPT);

  const auto connection_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  stream_info_.downstream_connection_info_provider_->setSslConnection(connection_info);

  for (uint64_t i = 0; i < 200; i++) {
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_LOG_CONTAINS("trace", "downstreamSslConnection is present", {
      EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    });

    filter_->onDestroy();
    setupFilter();
  }
}

TEST_F(LuaHttpFilterTest, ImportPublicKey) {
  const std::string SCRIPT{R"EOF(
    function string.fromhex(str)
      return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
      end))
    end
    function envoy_on_request(request_handle)
      key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a62cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001"
      raw = key:fromhex()
      key = request_handle:importPublicKey(raw, string.len(raw)):get()

      if key == nil then
        request_handle:logTrace("failed to import public key")
      else
        request_handle:logTrace("succeeded to import public key")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  EXPECT_LOG_CONTAINS("trace", "succeeded to import public key", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
}

TEST_F(LuaHttpFilterTest, InvalidPublicKey) {
  const std::string SCRIPT{R"EOF(
    function string.fromhex(str)
      return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
      end))
    end
    function envoy_on_request(request_handle)
      key = "0000"
      raw = key:fromhex()
      key = request_handle:importPublicKey(raw, string.len(raw)):get()

      if key == nil then
        request_handle:logTrace("failed to import public key")
      else
        request_handle:logTrace("succeeded to import public key")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  EXPECT_LOG_CONTAINS("trace", "failed to import public key", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
}

TEST_F(LuaHttpFilterTest, SignatureVerify) {
  const std::string SCRIPT{R"EOF(
    function string.fromhex(str)
      return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
      end))
    end
    function envoy_on_request(request_handle)
      key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a62cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001"
      hashFunc = "sha256"
      signature = "345ac3a167558f4f387a81c2d64234d901a7ceaa544db779d2f797b0ea4ef851b740905a63e2f4d5af42cee093a29c7155db9a63d3d483e0ef948f5ac51ce4e10a3a6606fd93ef68ee47b30c37491103039459122f78e1c7ea71a1a5ea24bb6519bca02c8c9915fe8be24927c91812a13db72dbcb500103a79e8f67ff8cb9e2a631974e0668ab3977bf570a91b67d1b6bcd5dce84055f21427d64f4256a042ab1dc8e925d53a769f6681a873f5859693a7728fcbe95beace1563b5ffbcd7c93b898aeba31421dafbfadeea50229c49fd6c445449314460f3d19150bd29a91333beaced557ed6295234f7c14fa46303b7e977d2c89ba8a39a46a35f33eb07a332"
      data = "hello"

      rawkey = key:fromhex()
      pubkey = request_handle:importPublicKey(rawkey, string.len(rawkey)):get()

      if pubkey == nil then
        request_handle:logTrace("failed to import public key")
        return
      end

      rawsig = signature:fromhex()

      ok, error = request_handle:verifySignature(hashFunc, pubkey, rawsig, string.len(rawsig), data, string.len(data))
      if ok then
        request_handle:logTrace("signature is valid")
      else
        request_handle:logTrace(error)
      end

      ok, error = request_handle:verifySignature("unknown", pubkey, rawsig, string.len(rawsig), data, string.len(data))
      if ok then
        request_handle:logTrace("signature is valid")
      else
        request_handle:logTrace(error)
      end

      ok, error = request_handle:verifySignature(hashFunc, pubkey, "0000", 4, data, string.len(data))
      if ok then
        request_handle:logTrace("signature is valid")
      else
        request_handle:logTrace(error)
      end

      ok, error = request_handle:verifySignature(hashFunc, pubkey, rawsig, string.len(rawsig), "xxxx", 4)
      if ok then
        request_handle:logTrace("signature is valid")
      else
        request_handle:logTrace(error)
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "signature is valid"},
                                 {"trace", "unknown is not supported."},
                                 {"trace", "Failed to verify digest. Error code: 0"},
                                 {"trace", "Failed to verify digest. Error code: 0"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
}

// Test whether the route configuration can properly disable the Lua filter.
TEST_F(LuaHttpFilterTest, LuaFilterDisabled) {
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(ADD_HEADERS_SCRIPT);
  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  per_route_proto_config.set_disabled(true);

  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers_1{{":path", "/"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_1, true));
  EXPECT_EQ("world", request_headers_1.get_("hello"));

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_config_.get()));

  Http::TestRequestHeaderMapImpl request_headers_2{{":path", "/"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_2, true));
  EXPECT_FALSE(request_headers_2.has("hello"));
}

// Test whether the automatic route cache clearing could be disabled.
TEST_F(LuaHttpFilterTest, DisableAutomaticRouteCacheClearing) {
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_clear_route_cache()->set_value(false);
  proto_config.mutable_default_source_code()->set_inline_string(ADD_HEADERS_SCRIPT);
  setupConfig(proto_config, {});
  setupFilter();

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers_1{{":path", "/"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_1, true));
  EXPECT_EQ("world", request_headers_1.get_("hello"));
}

TEST_F(LuaHttpFilterTest, LuaFilterContext) {
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  const std::string SCRIPT_WITH_ACCESS_FILTER_CONTEXT{R"EOF(
    function envoy_on_request(request_handle)
      if request_handle:filterContext():get("foo") == nil then
        request_handle:logTrace("foo in filter context is nil")
      else
        request_handle:logTrace(request_handle:filterContext():get("foo"))
      end
    end
    function envoy_on_response(response_handle)
      if response_handle:filterContext():get("foo") == nil then
        response_handle:logTrace("foo in filter context is nil")
      else
        response_handle:logTrace(response_handle:filterContext():get("foo"))
      end
    end
  )EOF"};
  proto_config.mutable_default_source_code()->set_inline_string(SCRIPT_WITH_ACCESS_FILTER_CONTEXT);

  {
    setupConfig(proto_config, {});
    setupFilter();

    ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(nullptr));

    Http::TestRequestHeaderMapImpl request_headers_1{{":path", "/"}};

    EXPECT_LOG_CONTAINS("trace", "foo in filter context is nil", {
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->decodeHeaders(request_headers_1, true));
    });
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_1, true));
    Http::TestResponseHeaderMapImpl response_headers_1{{":status", "200"}};
    EXPECT_LOG_CONTAINS("trace", "foo in filter context is nil", {
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->encodeHeaders(response_headers_1, true));
    });
    filter_->onDestroy();
  }
  {
    envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
    (*per_route_proto_config.mutable_filter_context()->mutable_fields())["foo"].set_string_value(
        "foo_value_in_filter_context");

    setupConfig(proto_config, per_route_proto_config);
    setupFilter();

    ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
        .WillByDefault(Return(per_route_config_.get()));

    Http::TestRequestHeaderMapImpl request_headers_2{{":path", "/"}};
    EXPECT_LOG_CONTAINS("trace", "foo_value_in_filter_context", {
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->decodeHeaders(request_headers_2, true));
    });

    Http::TestResponseHeaderMapImpl response_headers_2{{":status", "200"}};
    EXPECT_LOG_CONTAINS("trace", "foo_value_in_filter_context", {
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->encodeHeaders(response_headers_2, true));
    });
  }
}

// Test whether the route can directly reuse the Lua code in the global configuration.
TEST_F(LuaHttpFilterTest, LuaFilterRefSourceCodes) {
  const std::string SCRIPT_FOR_ROUTE_ONE{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:headers():add("route_info", "This request is routed by ROUTE_ONE");
    end
  )EOF"};
  const std::string SCRIPT_FOR_ROUTE_TWO{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:headers():add("route_info", "This request is routed by ROUTE_TWO");
    end
  )EOF"};
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(ADD_HEADERS_SCRIPT);
  envoy::config::core::v3::DataSource source1, source2;
  source1.set_inline_string(SCRIPT_FOR_ROUTE_ONE);
  source2.set_inline_string(SCRIPT_FOR_ROUTE_TWO);
  proto_config.mutable_source_codes()->insert({"route_one.lua", source1});
  proto_config.mutable_source_codes()->insert({"route_two.lua", source2});

  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  per_route_proto_config.set_name("route_two.lua");

  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_config_.get()));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ("This request is routed by ROUTE_TWO", request_headers.get_("route_info"));
}

// Test whether the route can directly reuse the Lua code in the global configuration.
TEST_F(LuaHttpFilterTest, LuaFilterWithInlinePerRouteSourceCode) {
  const std::string SCRIPT_FOR_ROUTE_ONE{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:headers():add("route_info", "This request is routed by ROUTE_ONE");
    end
  )EOF"};

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(ADD_HEADERS_SCRIPT);

  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  per_route_proto_config.mutable_source_code()->set_inline_string(SCRIPT_FOR_ROUTE_ONE);

  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_config_.get()));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ("This request is routed by ROUTE_ONE", request_headers.get_("route_info"));
}

// Lua filter do nothing when the referenced name does not exist.
TEST_F(LuaHttpFilterTest, LuaFilterRefSourceCodeNotExist) {
  const std::string SCRIPT_FOR_ROUTE_ONE{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:headers():add("route_info", "This request is routed by ROUTE_ONE");
    end
  )EOF"};

  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(ADD_HEADERS_SCRIPT);
  envoy::config::core::v3::DataSource source1;
  source1.set_inline_string(SCRIPT_FOR_ROUTE_ONE);
  proto_config.mutable_source_codes()->insert({"route_one.lua", source1});

  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  // The global source codes do not contain a script named 'route_two.lua'.
  per_route_proto_config.set_name("route_two.lua");

  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  ON_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(per_route_config_.get()));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_TRUE(request_headers.get(Http::LowerCaseString("hello")).empty());
}

TEST_F(LuaHttpFilterTest, LuaFilterBase64Escape) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local base64Encoded = request_handle:base64Escape("foobar")
      request_handle:logTrace(base64Encoded)
    end

    function envoy_on_response(response_handle)
      local base64Encoded = response_handle:base64Escape("barfoo")
      response_handle:logTrace(base64Encoded)

      local resp_body_buf = response_handle:body()
      local resp_body = resp_body_buf:getBytes(0, resp_body_buf:length())
      local b64_resp_body = response_handle:base64Escape(resp_body)
      response_handle:logTrace(b64_resp_body)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};

  EXPECT_LOG_CONTAINS("trace", "Zm9vYmFy", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "YmFyZm9v", {
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers, false));
  });

  // Base64 encoding should also work for binary data.
  uint8_t buffer[34] = {31, 139, 8,  0, 0, 0, 0, 0,   0,   255, 202, 72,  205, 201, 201, 47, 207,
                        47, 202, 73, 1, 4, 0, 0, 255, 255, 173, 32,  235, 249, 10,  0,   0,  0};
  Buffer::OwnedImpl response_body(buffer, 34);
  EXPECT_LOG_CONTAINS("trace", "H4sIAAAAAAAA/8pIzcnJL88vykkBBAAA//+tIOv5CgAAAA==", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  });
}

TEST_F(LuaHttpFilterTest, Timestamp_ReturnsFormatSet) {
  const std::string SCRIPT{R"EOF(
      function envoy_on_request(request_handle)
        request_handle:logTrace(request_handle:timestamp(EnvoyTimestampResolution.MILLISECOND))
        request_handle:logTrace(request_handle:timestamp("invalid_format"))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  // Explicitly set to milliseconds
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "1583879145572"},
                                 {"error", "timestamp format must be MILLISECOND."},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, Timestamp_DefaultsToMilliseconds_WhenNoFormatSet) {
  const std::string SCRIPT{R"EOF(
      function envoy_on_request(request_handle)
        request_handle:logTrace(request_handle:timestamp())
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "1583879145572", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, TimestampString) {
  const std::string SCRIPT{R"EOF(
      function envoy_on_request(request_handle)
        request_handle:logTrace(request_handle:timestampString(EnvoyTimestampResolution.MILLISECOND))
        request_handle:logTrace(request_handle:timestampString(EnvoyTimestampResolution.MICROSECOND))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "1583879145572"},
                                 {"trace", "1583879145572237"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, TimestampString_DefaultsToMilliseconds) {
  const std::string SCRIPT{R"EOF(
      function envoy_on_request(request_handle)
        request_handle:logTrace(request_handle:timestampString())
        request_handle:logTrace(request_handle:timestampString("invalid_format"))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "1583879145572"},
                                 {"error", "timestamp format must be MILLISECOND or MICROSECOND."},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, LuaFilterSetResponseBuffer) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      local content_length = response_handle:body():setBytes("1234")
      response_handle:logTrace(content_length)

      -- It is possible to replace an entry in headers after overridding encoding buffer.
      response_handle:headers():replace("content-length", content_length)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));
  Buffer::OwnedImpl response_body("1234567890");
  EXPECT_LOG_CONTAINS("trace", "4", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  });
  EXPECT_EQ(4, encoder_callbacks_.buffer_->length());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, LuaFilterSetResponseBufferChunked) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      local last
      for chunk in response_handle:bodyChunks() do
        chunk:setBytes("")
        last = chunk
      end
      response_handle:logTrace(last:setBytes("1234"))
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_body("1234567890");
  EXPECT_LOG_CONTAINS("trace", "4", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// BodyBuffer should not truncated when bodyBuffer set hex character
TEST_F(LuaHttpFilterTest, LuaBodyBufferSetBytesWithHex) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      local bodyBuffer = response_handle:body()
      bodyBuffer:setBytes("\x471111")
      local body_str = bodyBuffer:getBytes(0, bodyBuffer:length())
      response_handle:logTrace(body_str)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_body("");
  EXPECT_LOG_CONTAINS("trace", "G1111", {
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  });
  EXPECT_EQ(5, encoder_callbacks_.buffer_->length());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// BodyBuffer should not truncated when bodyBuffer set zero
TEST_F(LuaHttpFilterTest, LuaBodyBufferSetBytesWithZero) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      local bodyBuffer = response_handle:body()
      bodyBuffer:setBytes("\0")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl response_body("1111");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  EXPECT_EQ(1, encoder_callbacks_.buffer_->length());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Script logging a table instead of the expected string.
TEST_F(LuaHttpFilterTest, LogTableInsteadOfString) {
  const std::string LOG_TABLE{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace({})
    end
  )EOF"};

  InSequence s;
  setup(LOG_TABLE);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS(
      "error", "[string \"...\"]:3: bad argument #1 to 'logTrace' (string expected, got table)", {
        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                  filter_->decodeHeaders(request_headers, true));
      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, DestructFilterConfigPerRoute) {
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  per_route_proto_config.mutable_source_code()->set_inline_string(HEADER_ONLY_SCRIPT);
  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  InSequence s;
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).Times(0);
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_)).Times(0);

  per_route_config_ =
      std::make_shared<FilterConfigPerRoute>(per_route_proto_config, server_factory_context_);
  per_route_config_.reset();
}

TEST_F(LuaHttpFilterTest, Stats) {
  InSequence s;
  setup(REQUEST_RESPONSE_RUNTIME_ERROR_SCRIPT);

  // Request error
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: attempt to index global 'hello' (a nil value)",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, true));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());

  // Response error
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:7: attempt to index global 'bye' (a nil value)", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  });
  EXPECT_EQ(2, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(2, stats_store_.counter("test.lua.executions").value());
}

TEST_F(LuaHttpFilterTest, StatsWithPerFilterPrefix) {
  InSequence s;
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  proto_config.mutable_default_source_code()->set_inline_string(
      REQUEST_RESPONSE_RUNTIME_ERROR_SCRIPT);
  proto_config.mutable_stat_prefix()->assign("my_script");
  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  // Request error
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: attempt to index global 'hello' (a nil value)",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, true));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.my_script.errors").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.my_script.errors").value());

  // Response error
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:7: attempt to index global 'bye' (a nil value)", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  });
  EXPECT_EQ(2, stats_store_.counter("test.lua.my_script.errors").value());
}

// Test clear route cache.
TEST_F(LuaHttpFilterTest, ClearRouteCache) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:clearRouteCache()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test successful upstream host override
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHost) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("192.168.21.11", false)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_,
              setUpstreamOverrideHost(testing::Pair(testing::Eq("192.168.21.11"), false)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test upstream host override with strict flag set to true
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostStrict) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("192.168.21.11", true)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_,
              setUpstreamOverrideHost(testing::Pair(testing::Eq("192.168.21.11"), true)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test that setUpstreamOverrideHost requires a host argument
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostNoArgument) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error",
                      "[string \"...\"]:3: bad argument #1 to 'setUpstreamOverrideHost' "
                      "(string expected, got no value)",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, true));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test that setUpstreamOverrideHost validates the argument type for strict flag
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostInvalidStrictType) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("192.168.21.11", "not_a_boolean")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error",
                      "[string \"...\"]:3: bad argument #2 to 'setUpstreamOverrideHost' "
                      "(boolean expected, got string)",
                      {
                        EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                  filter_->decodeHeaders(request_headers, true));
                      });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test that setUpstreamOverrideHost can be called on different paths
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostDifferentPaths) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("192.168.21.11", true)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  {
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/path1"}};
    EXPECT_CALL(decoder_callbacks_,
                setUpstreamOverrideHost(testing::Pair(testing::Eq("192.168.21.11"), true)));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  }

  setupFilter();

  {
    Http::TestRequestHeaderMapImpl request_headers{{":path", "/path2"}};
    EXPECT_CALL(decoder_callbacks_,
                setUpstreamOverrideHost(testing::Pair(testing::Eq("192.168.21.11"), true)));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  }
}

// Test empty host argument
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostEmptyHost) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("", false)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: host is not a valid IP address", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test that setUpstreamOverrideHost rejects non-IP hosts
TEST_F(LuaHttpFilterTest, SetUpstreamOverrideHostNonIpHost) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:setUpstreamOverrideHost("example.com", false)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("error", "[string \"...\"]:3: host is not a valid IP address", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test accessing typed metadata from StreamInfo through Lua.
TEST_F(LuaHttpFilterTest, GetStreamInfoTypedMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("envoy.filters.http.set_metadata")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
        -- The typed metadata is structured with field keys
        if typed_meta.fields and typed_meta.fields.metadata_namespace then
          request_handle:logTrace("Metadata namespace: " .. typed_meta.fields.metadata_namespace.string_value)
        end
        if typed_meta.fields and typed_meta.fields.allow_overwrite then
          request_handle:logTrace("Allow overwrite: " .. tostring(typed_meta.fields.allow_overwrite.bool_value))
        end
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a Struct for testing typed metadata using the set_metadata filter's proto
  Protobuf::Struct main_struct;

  // Add simple key/value pairs
  (*main_struct.mutable_fields())["metadata_namespace"].set_string_value("test.namespace");
  (*main_struct.mutable_fields())["allow_overwrite"].set_bool_value(true);

  // Pack the Struct into an Any
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/google.protobuf.Struct");
  typed_config.PackFrom(main_struct);

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.http.set_metadata", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "Has typed metadata: true"},
                                 {"trace", "Metadata namespace: test.namespace"},
                                 {"trace", "Allow overwrite: true"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test accessing complex typed metadata with nested structures and arrays.
TEST_F(LuaHttpFilterTest, GetStreamInfoComplexTypedMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("envoy.filters.http.complex_metadata")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")

        -- The typed metadata is structured with a fields key
        if typed_meta.fields then
          -- Access configuration info (nested struct)
          if typed_meta.fields.config and typed_meta.fields.config.struct_value then
            local config = typed_meta.fields.config.struct_value.fields
            request_handle:logTrace("Config version: " .. config.version.string_value)
            request_handle:logTrace("Config enabled: " .. tostring(config.enabled.bool_value))
          end

          -- Access servers (array)
          if typed_meta.fields.servers and typed_meta.fields.servers.list_value then
            local servers = typed_meta.fields.servers.list_value.values
            for i, server in ipairs(servers) do
              request_handle:logTrace("Server " .. i .. ": " .. server.string_value)
            end
          end
        end
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a complex Struct for testing
  Protobuf::Struct main_struct;

  // Add simple key/value pairs
  (*main_struct.mutable_fields())["filter_name"].set_string_value("complex_metadata");
  (*main_struct.mutable_fields())["version"].set_string_value("v1.2.3");

  // Create a nested struct for config
  Protobuf::Struct config_struct;
  (*config_struct.mutable_fields())["version"].set_string_value("v2.0.0");
  (*config_struct.mutable_fields())["enabled"].set_bool_value(true);

  // Add the config to the main struct
  auto* config_value = &(*main_struct.mutable_fields())["config"];
  *config_value->mutable_struct_value() = config_struct;

  // Create a list for servers
  Protobuf::ListValue servers_list;
  servers_list.add_values()->set_string_value("server1.example.com");
  servers_list.add_values()->set_string_value("server2.example.com");

  // Add the servers list to the main struct
  auto* servers_value = &(*main_struct.mutable_fields())["servers"];
  *servers_value->mutable_list_value() = servers_list;

  // Pack the Struct into an Any
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/google.protobuf.Struct");
  typed_config.PackFrom(main_struct);

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.filters.http.complex_metadata", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "Has typed metadata: true"},
                                 {"trace", "Config version: v2.0.0"},
                                 {"trace", "Config enabled: true"},
                                 {"trace", "Server 1: server1.example.com"},
                                 {"trace", "Server 2: server2.example.com"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test accessing non-existent typed metadata.
TEST_F(LuaHttpFilterTest, GetStreamInfoTypedMetadataNotFound) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("unknown.filter")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS("trace", "Has typed metadata: false",
                      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test behavior when the type URL in typed metadata cannot be found in the Protobuf descriptor
// pool.
TEST_F(LuaHttpFilterTest, GetStreamInfoTypedMetadataInvalidType) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("envoy.test.metadata")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Pack an invalid/unknown message type
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/unknown.type");
  typed_config.set_value("invalid data");

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.test.metadata", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS("trace", "Has typed metadata: false",
                      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test behavior when the data in typed metadata cannot be unpacked.
TEST_F(LuaHttpFilterTest, GetStreamInfoTypedMetadataUnpackFailure) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local typed_meta = request_handle:streamInfo():dynamicTypedMetadata("envoy.test.metadata")
      if typed_meta then
        request_handle:logTrace("Has typed metadata: true")
      else
        request_handle:logTrace("Has typed metadata: false")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Pack invalid data that will fail to unpack
  Protobuf::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/google.protobuf.Struct");
  typed_config.set_value("invalid protobuf data");

  stream_info_.metadata_.mutable_typed_filter_metadata()->insert(
      {"envoy.test.metadata", typed_config});

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  EXPECT_LOG_CONTAINS("trace", "Has typed metadata: false",
                      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                filter_->decodeHeaders(request_headers, true)));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(1, stats_store_.counter("test.lua.executions").value());
}

// Test drainConnectionUponCompletion on request path.
TEST_F(LuaHttpFilterTest, DrainConnectionUponCompletionRequest) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:streamInfo():drainConnectionUponCompletion()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  Event::SimulatedTimeSystem test_time;
  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(stream_info.shouldDrainConnectionUponCompletion());
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_TRUE(stream_info.shouldDrainConnectionUponCompletion());
}

// Test drainConnectionUponCompletion on response path.
TEST_F(LuaHttpFilterTest, DrainConnectionUponCompletionResponse) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_response(response_handle)
      -- Check for Connection: close header from upstream.
      local connection_header = response_handle:headers():get("connection")
      if connection_header == "close" then
        response_handle:streamInfo():drainConnectionUponCompletion()
        response_handle:logTrace("drain_set_to_true")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Event::SimulatedTimeSystem test_time;
  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  // Verify initially false.
  EXPECT_FALSE(stream_info.shouldDrainConnectionUponCompletion());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"connection", "close"}};
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_LOG_CONTAINS("trace", "drain_set_to_true",
                      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                filter_->encodeHeaders(response_headers, true)));

  // Verify it was set to true.
  EXPECT_TRUE(stream_info.shouldDrainConnectionUponCompletion());
}

// Test that handle:virtualHost():metadata() works when both virtual host and route match.
// This verifies that when a virtual host is matched and a route is found for the request,
// the virtualHost() function returns a valid object and metadata can be accessed
// successfully from both request and response handles.
TEST_F(LuaHttpFilterTest, GetVirtualHostMetadataFromHandle) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local metadata = request_handle:virtualHost():metadata()
      request_handle:logTrace(metadata:get("foo.bar")["name"])
      request_handle:logTrace(metadata:get("foo.bar")["prop"])
    end
    function envoy_on_response(response_handle)
      local metadata = response_handle:virtualHost():metadata()
      response_handle:logTrace(metadata:get("baz.bat")["name"])
      response_handle:logTrace(metadata:get("baz.bat")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupVirtualHostMetadata(METADATA);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->encodeHeaders(response_headers, true));
                             });
}

// Test that handle:virtualHost():metadata() returns empty metadata when no filter-specific metadata
// exists. This verifies that when a virtual host has metadata for other filters but not for the
// current one, the metadata object is empty.
TEST_F(LuaHttpFilterTest, GetVirtualHostMetadataFromHandleNoLuaMetadata) {
  const std::string SCRIPT{R"EOF(
    function is_metadata_empty(metadata)
      for _, _ in pairs(metadata) do
        return false
      end
      return true
    end
    function envoy_on_request(request_handle)
      if is_metadata_empty(request_handle:virtualHost():metadata()) then
        request_handle:logTrace("No metadata found on request")
      end
    end
    function envoy_on_response(response_handle)
      if is_metadata_empty(response_handle:virtualHost():metadata()) then
        response_handle:logTrace("No metadata found on response")
      end
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.some_filter:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupVirtualHostMetadata(METADATA);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found on request", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found on response", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  });
}

// Test that handle:virtualHost() returns a valid virtual host wrapper object that can be
// safely accessed when no virtual host matches the request authority.
// This verifies that calling metadata() returns an empty metadata object.
TEST_F(LuaHttpFilterTest, GetVirtualHostFromHandleNoVirtualHost) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local virtual_host = request_handle:virtualHost()
      for _, _ in pairs(virtual_host:metadata()) do
        return
      end
      request_handle:logTrace("No metadata found during request handling")
    end
    function envoy_on_response(response_handle)
      local virtual_host = response_handle:virtualHost()
      for _, _ in pairs(virtual_host:metadata()) do
        return
      end
      response_handle:logTrace("No metadata found during response handling")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during request handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during response handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  });

  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(2, stats_store_.counter("test.lua.executions").value());
}

// Test that handle:virtualHost():metadata() still works when there is no route.
// This verifies that when a virtual host is matched but no route is found for the request,
// the virtualHost() function returns a valid object and metadata can still be accessed
// successfully from both request and response handles.
TEST_F(LuaHttpFilterTest, GetVirtualHostMetadataFromHandleNoRoute) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local metadata = request_handle:virtualHost():metadata()
      request_handle:logTrace(metadata:get("foo.bar")["name"])
      request_handle:logTrace(metadata:get("foo.bar")["prop"])
    end
    function envoy_on_response(response_handle)
      local metadata = response_handle:virtualHost():metadata()
      response_handle:logTrace(metadata:get("baz.bat")["name"])
      response_handle:logTrace(metadata:get("baz.bat")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupVirtualHostMetadata(METADATA);

  // Request path
  ON_CALL(decoder_callbacks_, route()).WillByDefault(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });

  // Response path
  ON_CALL(encoder_callbacks_, route()).WillByDefault(Return(nullptr));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->encodeHeaders(response_headers, true));
                             });
}

// Test that handle:route():metadata() returns metadata when route matches the request.
// This verifies that when a route is found for the request, the route() function returns
// a valid object and metadata can be successfully accessed from both request and response handles.
TEST_F(LuaHttpFilterTest, GetRouteMetadataFromHandle) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local metadata = request_handle:route():metadata()
      request_handle:logTrace(metadata:get("foo.bar")["name"])
      request_handle:logTrace(metadata:get("foo.bar")["prop"])
    end
    function envoy_on_response(response_handle)
      local metadata = response_handle:route():metadata()
      response_handle:logTrace(metadata:get("baz.bat")["name"])
      response_handle:logTrace(metadata:get("baz.bat")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupRouteMetadata(METADATA);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "foo"},
                                 {"trace", "bar"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->decodeHeaders(request_headers, true));
                             });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS_ALL_OF(Envoy::ExpectedLogMessages({
                                 {"trace", "baz"},
                                 {"trace", "bat"},
                             }),
                             {
                               EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                                         filter_->encodeHeaders(response_headers, true));
                             });
}

// Test that handle:route():metadata() returns empty metadata when no filter-specific metadata
// exists. This verifies that when a route has metadata for other filters but not for the
// current one, the metadata object is empty.
TEST_F(LuaHttpFilterTest, GetRouteMetadataFromHandleNoLuaMetadata) {
  const std::string SCRIPT{R"EOF(
    function is_metadata_empty(metadata)
      for _, _ in pairs(metadata) do
        return false
      end
      return true
    end
    function envoy_on_request(request_handle)
      if is_metadata_empty(request_handle:route():metadata()) then
        request_handle:logTrace("No metadata found during request handling")
      end
    end
    function envoy_on_response(response_handle)
      if is_metadata_empty(response_handle:route():metadata()) then
        response_handle:logTrace("No metadata found during response handling")
      end
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.some_filter:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupRouteMetadata(METADATA);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during request handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during response handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  });
}

// Test that handle:route() returns a valid route wrapper object that can be
// safely accessed when no route matches the request.
// This verifies that calling metadata() returns an empty metadata object.
TEST_F(LuaHttpFilterTest, GetRouteFromHandleNoRoute) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      local route = request_handle:route()
      for _, _ in pairs(route:metadata()) do
        return
      end
      request_handle:logTrace("No metadata found during request handling")
    end
    function envoy_on_response(response_handle)
      local route = response_handle:route()
      for _, _ in pairs(route:metadata()) do
        return
      end
      response_handle:logTrace("No metadata found during response handling")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Request path
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during request handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  });

  // Response path
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_LOG_CONTAINS("trace", "No metadata found during response handling", {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  });

  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
  EXPECT_EQ(2, stats_store_.counter("test.lua.executions").value());
}

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
