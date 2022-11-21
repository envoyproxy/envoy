#include <cstdint>
#include <memory>

#include "envoy/config/core/v3/base.pb.h"

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

class TestFilter : public Filter {
public:
  using Filter::Filter;

  MOCK_METHOD(void, scriptLog, (spdlog::level::level_enum level, absl::string_view message));
};

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

  void setupConfig(envoy::extensions::filters::http::lua::v3::Lua& proto_config,
                   envoy::extensions::filters::http::lua::v3::LuaPerRoute& per_route_proto_config) {
    // Setup filter config for Lua filter.
    config_ = std::make_shared<FilterConfig>(proto_config, tls_, cluster_manager_, api_,
                                             stats_store_, "test.");
    // Setup per route config for Lua filter.
    per_route_config_ =
        std::make_shared<FilterConfigPerRoute>(per_route_proto_config, server_factory_context_);
  }

  void setupFilter() {
    Event::SimulatedTimeSystem test_time;
    test_time.setSystemTime(std::chrono::microseconds(1583879145572237));

    filter_ = std::make_unique<TestFilter>(config_, test_time.timeSystem());
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
      FilterConfig(proto_config, tls, cluster_manager, api, stats_store, "lua"),
      Filters::Common::Lua::LuaException,
      "script load error: [string \"...\"]:3: '=' expected near '<eof>'");
}

// Script touching headers only, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestHeadersOnly) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script touching headers only, request that has body.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBody) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script touching headers only, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBodyTrailers) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for body chunks, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestHeadersOnly) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for body chunks, request that has body.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBody) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for body chunks, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBodyTrailers) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBody) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers without body, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers without body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBody) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for trailers without body, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestHeadersOnly) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBody) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body, request that has a body in multiple frames.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFrames) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Scripting asking for synchronous body, request that has a body in multiple frames follows by
// trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFramesTrailers) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data2, false));
  decoder_callbacks_.addDecodedData(data2, false);

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body and trailers, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestHeadersOnly) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body and trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBody) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// Script asking for synchronous body and trailers, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBodyTrailers) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:7: object used outside of proper scope")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:4: attempt to index local 'foo' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  TestFilter filter2(config_, test_time.timeSystem());
  EXPECT_CALL(filter2, scriptLog(spdlog::level::err,
                                 StrEq("[string \"...\"]:6: object used outside of proper scope")));
  filter2.decodeHeaders(request_headers, true);
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err, StrEq("script performed an unexpected yield")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:5: attempt to index local 'foo' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:5: object used outside of proper scope")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl continue_headers{{":status", "100"}};
  // No lua hooks for 100-continue
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("100"))).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encode1xxHeaders(continue_headers));

  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("200")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, false));

  Http::TestResponseTrailerMapImpl response_trailers{{"hello", "world"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("world")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("200")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  const char response[8] = {'r', 'e', 's', 'p', '\0', 'n', 's', 'e'};
  response_message->body().add(response, 8);
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("8")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(std::string("resp\0nse", 8))));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("0")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("nse")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(
      new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
          new Http::TestResponseHeaderMapImpl{{"key", "value"}, {"key", "second_value"}}}));

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("key value,second_value")));

  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  response_message->body().add("response");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("response")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("response")));
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
  callbacks->onSuccess(request, std::move(response_message));

  response_message = std::make_unique<Http::ResponseMessageImpl>(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "403"}}});
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 403")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
  callbacks->onSuccess(request, std::move(response_message));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"}, {":method", "GET"}, {":authority", "foo"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// HTTP call followed by immediate response.
TEST_F(LuaHttpFilterTest, HttpCallImmediateResponse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.lua_respond_with_send_local_reply", "false"}});
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
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(Eq("cluster")));
  EXPECT_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient());
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& cb,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            const Http::TestRequestHeaderMapImpl expected_headers{
                {":path", "/"}, {":method", "GET"}, {":authority", "foo"}};
            EXPECT_THAT(&message->headers(), HeaderMapEqualIgnoreOrder(&expected_headers));
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  Http::TestResponseHeaderMapImpl expected_headers{{":status", "403"},
                                                   {"set-cookie", "flavor=chocolate; Path=/"},
                                                   {"set-cookie", "variant=chewy; Path=/"}};
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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

  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:14: attempt to index local 'foo' (a nil value)")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 503")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("upstream failure")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onFailure(request, Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 503")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("upstream failure")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: http call timeout must be >= 0")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: http call timeout must be >= 0")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: http call cluster invalid. Must be configured")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
}

// HTTP request flow with timeout and sampled flag in options.
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
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestRequestTrailerMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::ResponseMessagePtr response_message(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onBeforeFinalizeUpstreamSpan(child_span_, &response_message->headers());
  callbacks->onSuccess(request, std::move(response_message));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
}

// HTTP request flow with timeout and sampled flag in options.
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
  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:3: \"invalid_option\" is not valid key for httpCall() options")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: http call headers must include "
                                        "':path', ':method', and ':authority'")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err, StrEq("[string \"...\"]:3: http call asynchronous flag "
                                                  "must be 'true', 'false', or empty")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
}

// Respond right away.
// This is also a regression test for https://github.com/envoyproxy/envoy/issues/3570 which runs
// the request flow 2000 times and does a GC at the end to make sure we don't leak memory.
TEST_F(LuaHttpFilterTest, ImmediateResponse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.lua_respond_with_send_local_reply", "false"}});
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
    Http::TestResponseHeaderMapImpl expected_headers{{":status", "503"}, {"content-length", "4"}};
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
    EXPECT_CALL(decoder_callbacks_, encodeData(_, true));
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: :status must be between 200-599")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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

  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:4: respond() cannot be called if headers have been continued")));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: respond not currently supported in the response path")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:4: cannot call bodyChunks after body processing has begun")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:4: cannot call body() after body has been streamed")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:4: cannot call body() after body streaming has started")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
      envoy.filters.http.lua:
        foo.bar:
          name: foo
          prop: bar
        baz.bat:
          name: baz
          prop: bat
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  setupMetadata(METADATA);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("foo")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("baz")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bat")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("ok")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("ok")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("HTTP/1.1")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("foo.example.com")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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

  ProtobufWkt::Value metadata_value;
  constexpr uint8_t buffer[] = {'h', 'e', 0x00, 'l', 'l', 'o'};
  metadata_value.set_string_value(reinterpret_cast<char const*>(buffer), sizeof(buffer));
  ProtobufWkt::Struct metadata;
  metadata.mutable_fields()->insert({"bin_data", metadata_value});
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.pp"] = metadata;

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info_));
  // Hex values for the buffer data
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::trace, StrEq("Hex Data: \\x68\\x65\\x00\\x6c\\x6c\\x6f")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time.timeSystem(), nullptr);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("abcd")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_EQ("bar", stream_info.dynamicMetadata()
                       .filter_metadata()
                       .at("envoy.lb")
                       .fields()
                       .at("foo")
                       .string_value());

  const ProtobufWkt::Struct& meta_complex = stream_info.dynamicMetadata()
                                                .filter_metadata()
                                                .at("envoy.lb")
                                                .fields()
                                                .at("complex")
                                                .struct_value();
  EXPECT_EQ("abcd", meta_complex.fields().at("x").string_value());
  EXPECT_EQ(1234.0, meta_complex.fields().at("y").number_value());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("plain")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  setupSecureConnection(true);
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("secure")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():ciphersuiteId())

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():validFromPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():expirationPeerCertificate())

        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():subjectLocalCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():sha256PeerCertificateDigest())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():serialNumberPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():issuerPeerCertificate())
        request_handle:logTrace(request_handle:streamInfo():downstreamSslConnection():subjectPeerCertificate())
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("peerCertificatePresented")));

  EXPECT_CALL(*connection_info, peerCertificateValidated()).WillOnce(Return(true));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("peerCertificateValidated")));

  const std::vector<std::string> peer_uri_sans{"peer-uri-sans-1", "peer-uri-sans-2"};
  EXPECT_CALL(*connection_info, uriSanPeerCertificate()).WillOnce(Return(peer_uri_sans));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("peer-uri-sans-1,peer-uri-sans-2")));

  const std::vector<std::string> local_uri_sans{"local-uri-sans-1", "local-uri-sans-2"};
  EXPECT_CALL(*connection_info, uriSanLocalCertificate()).WillOnce(Return(local_uri_sans));
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::trace, StrEq("local-uri-sans-1,local-uri-sans-2")));

  const std::vector<std::string> peer_dns_sans{"peer-dns-sans-1", "peer-dns-sans-2"};
  EXPECT_CALL(*connection_info, dnsSansPeerCertificate()).WillOnce(Return(peer_dns_sans));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("peer-dns-sans-1,peer-dns-sans-2")));

  const std::vector<std::string> local_dns_sans{"local-dns-sans-1", "local-dns-sans-2"};
  EXPECT_CALL(*connection_info, dnsSansLocalCertificate()).WillOnce(Return(local_dns_sans));
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::trace, StrEq("local-dns-sans-1,local-dns-sans-2")));

  const std::string subject_local = "subject-local";
  EXPECT_CALL(*connection_info, subjectLocalCertificate()).WillOnce(ReturnRef(subject_local));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(subject_local)));

  const uint64_t cipher_suite_id = 0x0707;
  EXPECT_CALL(*connection_info, ciphersuiteId()).WillRepeatedly(Return(cipher_suite_id));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("0x0707")));

  const SystemTime validity(std::chrono::seconds(1522796777));
  EXPECT_CALL(*connection_info, validFromPeerCertificate()).WillRepeatedly(Return(validity));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1522796777")));

  const SystemTime expiry(std::chrono::seconds(1522796776));
  EXPECT_CALL(*connection_info, expirationPeerCertificate()).WillRepeatedly(Return(expiry));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1522796776")));

  const std::string peer_cert_digest = "peer-cert-digest";
  EXPECT_CALL(*connection_info, sha256PeerCertificateDigest())
      .WillOnce(ReturnRef(peer_cert_digest));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert_digest)));

  const std::string peer_cert_serial_number = "peer-cert-serial-number";
  EXPECT_CALL(*connection_info, serialNumberPeerCertificate())
      .WillOnce(ReturnRef(peer_cert_serial_number));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert_serial_number)));

  const std::string peer_cert_issuer = "peer-cert-issuer";
  EXPECT_CALL(*connection_info, issuerPeerCertificate()).WillOnce(ReturnRef(peer_cert_issuer));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert_issuer)));

  const std::string peer_cert_subject = "peer-cert-subject";
  EXPECT_CALL(*connection_info, subjectPeerCertificate()).WillOnce(ReturnRef(peer_cert_subject));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert_subject)));

  const std::string cipher_suite = "cipher-suite";
  EXPECT_CALL(*connection_info, ciphersuiteString()).WillOnce(Return(cipher_suite));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(cipher_suite)));

  const std::string tls_version = "tls-version";
  EXPECT_CALL(*connection_info, tlsVersion()).WillOnce(ReturnRef(tls_version));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(tls_version)));

  const std::string peer_cert = "peer-cert";
  EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificate())
      .WillOnce(ReturnRef(peer_cert));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert)));

  const std::string peer_cert_chain = "peer-cert-chain";
  EXPECT_CALL(*connection_info, urlEncodedPemEncodedPeerCertificateChain())
      .WillOnce(ReturnRef(peer_cert_chain));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(peer_cert_chain)));

  const std::string id = "12345";
  EXPECT_CALL(*connection_info, sessionId()).WillRepeatedly(ReturnRef(id));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(id)));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("downstreamSslConnection is nil")));

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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
    EXPECT_CALL(*filter_,
                scriptLog(spdlog::level::trace, StrEq("downstreamSslConnection is present")));

    Http::TestRequestHeaderMapImpl request_headers{{":path", "/"}};
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("succeeded to import public key")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("failed to import public key")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("signature is valid")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("unknown is not supported.")));
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::trace, StrEq("Failed to verify digest. Error code: 0")));
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::trace, StrEq("Failed to verify digest. Error code: 0")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("Zm9vYmFy")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("YmFyZm9v")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  // Base64 encoding should also work for binary data.
  uint8_t buffer[34] = {31, 139, 8,  0, 0, 0, 0, 0,   0,   255, 202, 72,  205, 201, 201, 47, 207,
                        47, 202, 73, 1, 4, 0, 0, 255, 255, 173, 32,  235, 249, 10,  0,   0,  0};
  Buffer::OwnedImpl response_body(buffer, 34);
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace,
                                  StrEq("H4sIAAAAAAAA/8pIzcnJL88vykkBBAAA//+tIOv5CgAAAA==")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1583879145572")));
  // Invalid format
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err, HasSubstr("timestamp format must be MILLISECOND.")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1583879145572")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1583879145572")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1583879145572237")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("1583879145572")));
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        HasSubstr("timestamp format must be MILLISECOND or MICROSECOND.")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("4")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  EXPECT_EQ(4, encoder_callbacks_.buffer_->length());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("4")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("G1111")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(response_body, true));
  EXPECT_EQ(5, encoder_callbacks_.buffer_->length());
  EXPECT_EQ(0, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:3: bad argument #1 to 'logTrace' (string expected, got table)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());
}

TEST_F(LuaHttpFilterTest, DestructFilterConfigPerRoute) {
  envoy::extensions::filters::http::lua::v3::Lua proto_config;
  envoy::extensions::filters::http::lua::v3::LuaPerRoute per_route_proto_config;
  per_route_proto_config.mutable_source_code()->set_inline_string(HEADER_ONLY_SCRIPT);
  setupConfig(proto_config, per_route_proto_config);
  setupFilter();

  InSequence s;
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).WillOnce(Return(false));
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_));
  EXPECT_CALL(server_factory_context_.dispatcher_, isThreadSafe()).WillOnce(Return(true));
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: attempt to index global 'hello' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.errors").value());

  // Response error
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:7: attempt to index global 'bye' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(2, stats_store_.counter("test.lua.errors").value());
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
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: attempt to index global 'hello' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, stats_store_.counter("test.lua.my_script.errors").value());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(1, stats_store_.counter("test.lua.my_script.errors").value());

  // Response error
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:7: attempt to index global 'bye' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(2, stats_store_.counter("test.lua.my_script.errors").value());
}

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
