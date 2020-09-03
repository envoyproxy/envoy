#include "envoy/config/core/v3/base.pb.h"

#include "common/http/utility.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/filters/http/lua/wrappers.h"

#include "test/extensions/filters/common/lua/lua_wrappers.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

using testing::InSequence;
using testing::ReturnPointee;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {
namespace {

class LuaHeaderMapWrapperTest : public Filters::Common::Lua::LuaWrappersTestBase<HeaderMapWrapper> {
public:
  void setup(const std::string& script) override {
    Filters::Common::Lua::LuaWrappersTestBase<HeaderMapWrapper>::setup(script);
    state_->registerType<HeaderMapIterator>();
  }
};

// Basic methods test for the header wrapper.
TEST_F(LuaHeaderMapWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:add("HELLO", "WORLD")
      testPrint(object:get("hELLo"))

      object:add("header1", "")
      object:add("header2", "foo")

      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end

      object:remove("header1")
      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end

      object:add("header3", "foo")
      object:add("header3", "bar")
      testPrint(object:get("header3"))
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(*this, testPrint("WORLD"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header1' ''"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  EXPECT_CALL(*this, testPrint("foo,bar"));
  start("callMe");
}

// Test modifiable methods.
TEST_F(LuaHeaderMapWrapperTest, ModifiableMethods) {
  const std::string SCRIPT{R"EOF(
    function shouldBeOk(object)
      object:get("hELLo")
      for key, value in pairs(object) do
      end
    end

    function shouldFailRemove(object)
      object:remove("foo")
    end

    function shouldFailAdd(object)
      object:add("foo")
    end

    function shouldFailReplace(object)
      object:replace("foo")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  start("shouldBeOk");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailRemove"), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:9: header map can no longer be modified");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailAdd"), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:13: header map can no longer be modified");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailReplace"), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:17: header map can no longer be modified");
}

// Verify that replace works correctly with both inline and normal headers.
TEST_F(LuaHeaderMapWrapperTest, Replace) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:replace(":path", "/new_path")
      object:replace("other_header", "other_header_value")
      object:replace("new_header", "new_header_value")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers{{":path", "/"}, {"other_header", "hello"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  start("callMe");

  EXPECT_EQ((Http::TestHeaderMapImpl{{":path", "/new_path"},
                                     {"other_header", "other_header_value"},
                                     {"new_header", "new_header_value"}}),
            headers);
}

// Modify during iteration.
TEST_F(LuaHeaderMapWrapperTest, ModifyDuringIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      for key, value in pairs(object) do
        object:add("hello", "world")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers{{"foo", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_THROW_WITH_MESSAGE(start("callMe"), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:4: header map cannot be modified while iterating");
}

// Modify after iteration.
TEST_F(LuaHeaderMapWrapperTest, ModifyAfterIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end

      object:add("hello", "world")

      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers{{"foo", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(*this, testPrint("'foo' 'bar'"));
  EXPECT_CALL(*this, testPrint("'foo' 'bar'"));
  EXPECT_CALL(*this, testPrint("'hello' 'world'"));
  start("callMe");
}

// Don't finish iteration.
TEST_F(LuaHeaderMapWrapperTest, DontFinishIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      iterator = pairs(object)
      key, value = iterator()
      iterator2 = pairs(object)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), Filters::Common::Lua::LuaException,
      "[string \"...\"]:5: cannot create a second iterator before completing the first");
}

// Use iterator across yield.
TEST_F(LuaHeaderMapWrapperTest, IteratorAcrossYield) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      iterator = pairs(object)
      coroutine.yield()
      iterator()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
  Filters::Common::Lua::LuaDeathRef<HeaderMapWrapper> wrapper(
      HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; }), true);
  yield_callback_ = [] {};
  start("callMe");
  wrapper.reset();
  EXPECT_THROW_WITH_MESSAGE(coroutine_->resume(0, [] {}), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:5: object used outside of proper scope");
}

class LuaStreamInfoWrapperTest
    : public Filters::Common::Lua::LuaWrappersTestBase<StreamInfoWrapper> {
public:
  void setup(const std::string& script) override {
    Filters::Common::Lua::LuaWrappersTestBase<StreamInfoWrapper>::setup(script);
    state_->registerType<DynamicMetadataMapWrapper>();
    state_->registerType<DynamicMetadataMapIterator>();
  }

protected:
  void expectToPrintCurrentProtocol(const absl::optional<Envoy::Http::Protocol>& protocol) {
    const std::string SCRIPT{R"EOF(
      function callMe(object)
        testPrint(string.format("'%s'", object:protocol()))
      end
    )EOF"};

    InSequence s;
    setup(SCRIPT);

    NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
    ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));
    Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
        StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
    EXPECT_CALL(*this,
                testPrint(fmt::format("'{}'", Http::Utility::getProtocolString(protocol.value()))));
    start("callMe");
    wrapper.reset();
  }

  envoy::config::core::v3::Metadata parseMetadataFromYaml(const std::string& yaml_string) {
    envoy::config::core::v3::Metadata metadata;
    TestUtility::loadFromYaml(yaml_string, metadata);
    return metadata;
  }

  Event::SimulatedTimeSystem test_time_;
};

// Return the current request protocol.
TEST_F(LuaStreamInfoWrapperTest, ReturnCurrentProtocol) {
  expectToPrintCurrentProtocol(Http::Protocol::Http10);
  expectToPrintCurrentProtocol(Http::Protocol::Http11);
  expectToPrintCurrentProtocol(Http::Protocol::Http2);
}

// Set, get and iterate stream info dynamic metadata.
TEST_F(LuaStreamInfoWrapperTest, SetGetAndIterateDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
      function callMe(object)
        testPrint(type(object:dynamicMetadata()))
        object:dynamicMetadata():set("envoy.lb", "foo", "bar")
        object:dynamicMetadata():set("envoy.lb", "so", "cool")

        testPrint(object:dynamicMetadata():get("envoy.lb")["foo"])
        testPrint(object:dynamicMetadata():get("envoy.lb")["so"])

        for filter, entry in pairs(object:dynamicMetadata()) do
          for key, value in pairs(entry) do
            testPrint(string.format("'%s' '%s'", key, value))
          end
        end

        local function nRetVals(...)
          return select('#',...)
        end
        testPrint(tostring(nRetVals(object:dynamicMetadata():get("envoy.ngx"))))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(*this, testPrint("userdata"));
  EXPECT_CALL(*this, testPrint("bar"));
  EXPECT_CALL(*this, testPrint("cool"));
  EXPECT_CALL(*this, testPrint("'foo' 'bar'"));
  EXPECT_CALL(*this, testPrint("'so' 'cool'"));
  EXPECT_CALL(*this, testPrint("0"));
  start("callMe");

  EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_EQ("bar", stream_info.dynamicMetadata()
                       .filter_metadata()
                       .at("envoy.lb")
                       .fields()
                       .at("foo")
                       .string_value());
  wrapper.reset();
}

// Set, get complex key/values in stream info dynamic metadata.
TEST_F(LuaStreamInfoWrapperTest, SetGetComplexDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
      function callMe(object)
        object:dynamicMetadata():set("envoy.lb", "foo", {x=1234, y="baz", z=true})
        object:dynamicMetadata():set("envoy.lb", "so", {"cool", "and", "dynamic", true})

        testPrint(tostring(object:dynamicMetadata():get("envoy.lb")["foo"].x))
        testPrint(object:dynamicMetadata():get("envoy.lb")["foo"].y)
        testPrint(tostring(object:dynamicMetadata():get("envoy.lb")["foo"].z))
        testPrint(object:dynamicMetadata():get("envoy.lb")["so"][1])
        testPrint(object:dynamicMetadata():get("envoy.lb")["so"][2])
        testPrint(object:dynamicMetadata():get("envoy.lb")["so"][3])
        testPrint(tostring(object:dynamicMetadata():get("envoy.lb")["so"][4]))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(*this, testPrint("1234"));
  EXPECT_CALL(*this, testPrint("baz"));
  EXPECT_CALL(*this, testPrint("true"));
  EXPECT_CALL(*this, testPrint("cool"));
  EXPECT_CALL(*this, testPrint("and"));
  EXPECT_CALL(*this, testPrint("dynamic"));
  EXPECT_CALL(*this, testPrint("true"));
  start("callMe");

  EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
  const ProtobufWkt::Struct& meta_foo = stream_info.dynamicMetadata()
                                            .filter_metadata()
                                            .at("envoy.lb")
                                            .fields()
                                            .at("foo")
                                            .struct_value();

  EXPECT_EQ(1234.0, meta_foo.fields().at("x").number_value());
  EXPECT_EQ("baz", meta_foo.fields().at("y").string_value());
  EXPECT_EQ(true, meta_foo.fields().at("z").bool_value());

  const ProtobufWkt::ListValue& meta_so =
      stream_info.dynamicMetadata().filter_metadata().at("envoy.lb").fields().at("so").list_value();

  EXPECT_EQ(4, meta_so.values_size());
  EXPECT_EQ("cool", meta_so.values(0).string_value());
  EXPECT_EQ("and", meta_so.values(1).string_value());
  EXPECT_EQ("dynamic", meta_so.values(2).string_value());
  EXPECT_EQ(true, meta_so.values(3).bool_value());

  wrapper.reset();
}

// Bad types in table
TEST_F(LuaStreamInfoWrapperTest, BadTypesInTableForDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:dynamicMetadata():set("envoy.lb", "hello", {x="world", y=function(a, b) end})
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_THROW_WITH_MESSAGE(start("callMe"), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:3: unexpected type 'function' in dynamicMetadata");
}

// Modify during iteration.
TEST_F(LuaStreamInfoWrapperTest, ModifyDuringIterationForDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:dynamicMetadata():set("envoy.lb", "hello", "world")
      for key, value in pairs(object:dynamicMetadata()) do
        object:dynamicMetadata():set("envoy.lb", "hello", "envoy")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), Filters::Common::Lua::LuaException,
      "[string \"...\"]:5: dynamic metadata map cannot be modified while iterating");
}

// Modify after iteration.
TEST_F(LuaStreamInfoWrapperTest, ModifyAfterIterationForDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:dynamicMetadata():set("envoy.lb", "hello", "world")
      for filter, entry in pairs(object:dynamicMetadata()) do
        testPrint(filter)
        for key, value in pairs(entry) do
          testPrint(string.format("'%s' '%s'", key, value))
        end
      end

      object:dynamicMetadata():set("envoy.lb", "hello", "envoy")
      object:dynamicMetadata():set("envoy.proxy", "proto", "grpc")
      for filter, entry in pairs(object:dynamicMetadata()) do
        testPrint(filter)
        for key, value in pairs(entry) do
          testPrint(string.format("'%s' '%s'", key, value))
        end
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(*this, testPrint("envoy.lb"));
  EXPECT_CALL(*this, testPrint("'hello' 'world'"));
  EXPECT_CALL(*this, testPrint("envoy.proxy"));
  EXPECT_CALL(*this, testPrint("'proto' 'grpc'"));
  EXPECT_CALL(*this, testPrint("envoy.lb"));
  EXPECT_CALL(*this, testPrint("'hello' 'envoy'"));
  start("callMe");
}

// Don't finish iteration.
TEST_F(LuaStreamInfoWrapperTest, DontFinishIterationForDynamicMetadata) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:dynamicMetadata():set("envoy.lb", "foo", "bar")
      iterator = pairs(object:dynamicMetadata())
      key, value = iterator()
      iterator2 = pairs(object:dynamicMetadata())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), Filters::Common::Lua::LuaException,
      "[string \"...\"]:6: cannot create a second iterator before completing the first");
}

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
