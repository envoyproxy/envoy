#include "common/buffer/buffer_impl.h"
#include "common/http/message_impl.h"
#include "common/request_info/request_info_impl.h"

#include "extensions/filters/http/lua/lua_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::StrEq;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

class TestFilter : public Filter {
public:
  using Filter::Filter;

  MOCK_METHOD2(scriptLog, void(spdlog::level::level_enum level, const char* message));
};

class LuaHttpFilterTest : public testing::Test {
public:
  LuaHttpFilterTest() {
    // Avoid strict mock failures for the following calls. We want strict for other calls.
    EXPECT_CALL(decoder_callbacks_, addDecodedData(_, _))
        .Times(AtLeast(0))
        .WillRepeatedly(Invoke([this](Buffer::Instance& data, bool) {
          if (decoder_callbacks_.buffer_ == nullptr) {
            decoder_callbacks_.buffer_.reset(new Buffer::OwnedImpl());
          }
          decoder_callbacks_.buffer_->move(data);
        }));

    EXPECT_CALL(decoder_callbacks_, decodingBuffer()).Times(AtLeast(0));
    EXPECT_CALL(decoder_callbacks_, route()).Times(AtLeast(0));

    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, _))
        .Times(AtLeast(0))
        .WillRepeatedly(Invoke([this](Buffer::Instance& data, bool) {
          if (encoder_callbacks_.buffer_ == nullptr) {
            encoder_callbacks_.buffer_.reset(new Buffer::OwnedImpl());
          }
          encoder_callbacks_.buffer_->move(data);
        }));
    EXPECT_CALL(encoder_callbacks_, encodingBuffer()).Times(AtLeast(0));
  }

  ~LuaHttpFilterTest() { filter_->onDestroy(); }

  void setup(const std::string& lua_code) {
    config_.reset(new FilterConfig(lua_code, tls_, cluster_manager_));
    setupFilter();
  }

  void setupFilter() {
    filter_.reset(new TestFilter(config_));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupSecureConnection(const bool secure) {
    EXPECT_CALL(decoder_callbacks_, connection()).WillOnce(Return(&connection_));
    EXPECT_CALL(Const(connection_), ssl()).Times(1).WillOnce(Return(secure ? &ssl_ : nullptr));
  }

  void setupMetadata(const std::string& yaml) {
    MessageUtil::loadFromYaml(yaml, metadata_);
    EXPECT_CALL(decoder_callbacks_.route_->route_entry_, metadata())
        .WillOnce(testing::ReturnRef(metadata_));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Upstream::MockClusterManager cluster_manager_;
  std::shared_ptr<FilterConfig> config_;
  std::unique_ptr<TestFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  envoy::api::v2::core::Metadata metadata_;
  NiceMock<Envoy::Ssl::MockConnection> ssl_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info_;

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
};

// Bad code in initial config.
TEST(LuaHttpFilterConfigTest, BadCode) {
  const std::string SCRIPT{R"EOF(
    bad
  )EOF"};

  NiceMock<ThreadLocal::MockInstance> tls;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  EXPECT_THROW_WITH_MESSAGE(FilterConfig(SCRIPT, tls, cluster_manager),
                            Filters::Common::Lua::LuaException,
                            "script load error: [string \"...\"]:3: '=' expected near '<eof>'");
}

// Script touching headers only, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestHeadersOnly) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script touching headers only, request that has body.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBody) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script touching headers only, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptHeadersOnlyRequestBodyTrailers) {
  InSequence s;
  setup(HEADER_ONLY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

// Script asking for body chunks, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestHeadersOnly) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for body chunks, request that has body.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBody) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script asking for body chunks, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyChunksRequestBodyTrailers) {
  InSequence s;
  setup(BODY_CHUNK_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("done")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

// Script asking for trailers, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBody) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script asking for trailers, request that has body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

// Script asking for trailers without body, request is headers only.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestHeadersOnly) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for trailers without body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBody) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script asking for trailers without body, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptTrailersNoBodyRequestBodyTrailers) {
  InSequence s;
  setup(TRAILERS_NO_BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

// Script asking for blocking body, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestHeadersOnly) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for blocking body, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBody) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script asking for blocking body, request that has a body in multiple frames.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFrames) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, true));
}

// Scripting asking for blocking body, request that has a body in multiple frames follows by
// trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyRequestBodyTwoFramesTrailers) {
  InSequence s;
  setup(BODY_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Buffer::OwnedImpl data2("world");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data2, false));
  decoder_callbacks_.addDecodedData(data2, false);

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

// Script asking for blocking body and trailers, request that is headers only.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestHeadersOnly) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Script asking for blocking body and trailers, request that has a body.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBody) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

// Script asking for blocking body and trailers, request that has a body and trailers.
TEST_F(LuaHttpFilterTest, ScriptBodyTrailersRequestBodyTrailers) {
  InSequence s;
  setup(BODY_TRAILERS_SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));
  decoder_callbacks_.addDecodedData(data, false);

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data1, false));

  Buffer::OwnedImpl data2("world");
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:7: object used outside of proper scope")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, false));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:4: attempt to index local 'foo' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  TestFilter filter2(config_);
  EXPECT_CALL(filter2, scriptLog(spdlog::level::err,
                                 StrEq("[string \"...\"]:6: object used outside of proper scope")));
  filter2.decodeHeaders(request_headers, true);
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err, StrEq("script performed an unexpected yield")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:5: attempt to index local 'foo' (a nil value)")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:5: object used outside of proper scope")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("/")));
  EXPECT_CALL(decoder_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("5")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestHeaderMapImpl continue_headers{{":status", "100"}};
  // No lua hooks for 100-continue
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("100"))).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(continue_headers));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("200")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, false));

  Http::TestHeaderMapImpl response_trailers{{"hello", "world"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("world")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
}

// Response blocking body.
TEST_F(LuaHttpFilterTest, ResponseBlockingBody) {
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("200")));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers, false));

  Buffer::OwnedImpl data2("helloworld");
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("10")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no trailers")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));
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
          [":authority"] = "foo"
        },
        "hello world",
        5000)
      for key, value in pairs(headers) do
        request_handle:logTrace(key .. " " .. value)
      end
      request_handle:logTrace(body)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{{":path", "/"},
                                               {":method", "POST"},
                                               {":authority", "foo"},
                                               {"content-length", "11"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_message->body().reset(new Buffer::OwnedImpl("response"));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("response")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(std::move(response_message));
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
  setup(SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{{":path", "/"},
                                               {":method", "POST"},
                                               {":authority", "foo"},
                                               {"content-length", "11"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_message->body().reset(new Buffer::OwnedImpl("response"));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("response")));
  EXPECT_CALL(cluster_manager_, get("cluster2"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster2"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":path", "/bar"}, {":method", "GET"}, {":authority", "foo"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));
  callbacks->onSuccess(std::move(response_message));

  response_message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "403"}}}));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 403")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(std::move(response_message));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":path", "/"}, {":method", "GET"}, {":authority", "foo"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 200")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("no body")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(std::move(response_message));
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
        {[":status"] = "403"},
        nil)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":path", "/"}, {":method", "GET"}, {":authority", "foo"}}),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  Http::TestHeaderMapImpl expected_headers{{":status", "403"}};
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));
  callbacks->onSuccess(std::move(response_message));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));

  EXPECT_CALL(*filter_,
              scriptLog(spdlog::level::err,
                        StrEq("[string \"...\"]:14: attempt to index local 'foo' (a nil value)")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onSuccess(std::move(response_message));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 503")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("upstream failure")));
  EXPECT_CALL(decoder_callbacks_, continueDecoding());
  callbacks->onFailure(Http::AsyncClient::FailureReason::Reset);
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(request, cancel());
  filter_->onDestroy();
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  Http::MockAsyncClientRequest request(&cluster_manager_.async_client_);
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(cluster_manager_, httpAsyncClientForCluster("cluster"));
  EXPECT_CALL(cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            cb.onFailure(Http::AsyncClient::FailureReason::Reset);
            return nullptr;
          }));

  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq(":status 503")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("upstream failure")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: http call timeout must be >= 0")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(cluster_manager_, get("cluster")).WillOnce(Return(nullptr));
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: http call cluster invalid. Must be configured")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(cluster_manager_, get("cluster"));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: http call headers must include "
                                        "':path', ':method', and ':authority'")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
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
  config_->runtimeGC();
  const uint64_t mem_use_at_start = config_->runtimeBytesUsed();

  for (uint64_t i = 0; i < 2000; i++) {
    Http::TestHeaderMapImpl request_headers{{":path", "/"}};
    Http::TestHeaderMapImpl expected_headers{{":status", "503"}, {"content-length", "4"}};
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
  config_->runtimeGC();
  EXPECT_TRUE(config_->runtimeBytesUsed() < mem_use_at_start * 2);
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::err,
                                  StrEq("[string \"...\"]:3: :status must be between 200-599")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:4: respond() cannot be called if headers have been continued")));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:3: respond not currently supported in the response path")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(request_headers, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(
      *filter_,
      scriptLog(
          spdlog::level::err,
          StrEq("[string \"...\"]:4: cannot call bodyChunks after body processing has begun")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));

  Http::TestHeaderMapImpl request_trailers{{"foo", "bar"}};
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:4: cannot call body() after body has been streamed")));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(
      *filter_,
      scriptLog(spdlog::level::err,
                StrEq("[string \"...\"]:4: cannot call body() after body streaming has started")));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
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
      envoy.lua:
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("foo")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("baz")));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bat")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("ok")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Get the current protocol.
TEST_F(LuaHttpFilterTest, GetCurrentProtocol) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:logTrace(request_handle:requestInfo():protocol())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillOnce(ReturnRef(request_info_));
  EXPECT_CALL(request_info_, protocol()).WillOnce(Return(Http::Protocol::Http11));

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("HTTP/1.1")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Set and get request info dynamic metadata.
TEST_F(LuaHttpFilterTest, SetGetDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function envoy_on_request(request_handle)
      request_handle:requestInfo():dynamicMetadata():set("envoy.lb", "foo", "bar")
      request_handle:logTrace(request_handle:requestInfo():dynamicMetadata():get("envoy.lb")["foo"])
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};
  DangerousDeprecatedTestTime test_time;
  RequestInfo::RequestInfoImpl request_info(Http::Protocol::Http2, test_time.timeSystem());
  EXPECT_EQ(0, request_info.dynamicMetadata().filter_metadata_size());
  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillOnce(ReturnRef(request_info));
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("bar")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, request_info.dynamicMetadata().filter_metadata_size());
  EXPECT_EQ("bar", request_info.dynamicMetadata()
                       .filter_metadata()
                       .at("envoy.lb")
                       .fields()
                       .at("foo")
                       .string_value());
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

  Http::TestHeaderMapImpl request_headers{{":path", "/"}};

  setupSecureConnection(false);
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("plain")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  setupSecureConnection(true);
  EXPECT_CALL(*filter_, scriptLog(spdlog::level::trace, StrEq("secure")));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
