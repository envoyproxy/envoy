#include "envoy/config/core/v3/base.pb.h"

#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/lua/wrappers.h"

#include "test/extensions/filters/common/lua/lua_wrappers.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

using testing::Expectation;
using testing::InSequence;
using testing::ReturnPointee;
using testing::ReturnRef;

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

  Http::TestRequestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(printer_, testPrint("WORLD"));
  EXPECT_CALL(printer_, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(printer_, testPrint("'header1' ''"));
  EXPECT_CALL(printer_, testPrint("'header2' 'foo'"));
  EXPECT_CALL(printer_, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(printer_, testPrint("'header2' 'foo'"));
  EXPECT_CALL(printer_, testPrint("foo,bar"));
  start("callMe");
}

// Get the total number of values for a certain header with multiple values.
TEST_F(LuaHeaderMapWrapperTest, GetNumValues) {
  const std::string SCRIPT{R"EOF(
      function callMe(object)
        testPrint(object:getNumValues("X-Test"))
        testPrint(object:getNumValues(":path"))
        testPrint(object:getNumValues("foobar"))
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"x-test", "foo"}, {"x-test", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(printer_, testPrint("2"));
  EXPECT_CALL(printer_, testPrint("1"));
  EXPECT_CALL(printer_, testPrint("0"));
  start("callMe");
}

// Get the value on a certain index for a header with multiple values.
TEST_F(LuaHeaderMapWrapperTest, GetAtIndex) {
  const std::string SCRIPT{R"EOF(
        function callMe(object)
          if object:getAtIndex("x-test", -1) == nil then
            testPrint("invalid_negative_index")
          end
          testPrint(object:getAtIndex("X-Test", 0))
          testPrint(object:getAtIndex("x-test", 1))
          testPrint(object:getAtIndex("x-test", 2))
          if object:getAtIndex("x-test", 3) == nil then
            testPrint("nil_value")
          end
        end
      )EOF"};

  InSequence s;
  setup(SCRIPT);

  Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {"x-test", "foo"}, {"x-test", "bar"}, {"x-test", ""}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(printer_, testPrint("invalid_negative_index"));
  EXPECT_CALL(printer_, testPrint("foo"));
  EXPECT_CALL(printer_, testPrint("bar"));
  EXPECT_CALL(printer_, testPrint(""));
  EXPECT_CALL(printer_, testPrint("nil_value"));
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

  Http::TestRequestHeaderMapImpl headers;
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

  Http::TestRequestHeaderMapImpl headers{{":path", "/"}, {"other_header", "hello"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  start("callMe");

  EXPECT_EQ((Http::TestRequestHeaderMapImpl{{":path", "/new_path"},
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

  Http::TestRequestHeaderMapImpl headers{{"foo", "bar"}};
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

  Http::TestRequestHeaderMapImpl headers{{"foo", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(printer_, testPrint("'foo' 'bar'"));
  EXPECT_CALL(printer_, testPrint("'foo' 'bar'"));
  EXPECT_CALL(printer_, testPrint("'hello' 'world'"));
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

  Http::TestRequestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
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

  Http::TestRequestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
  Filters::Common::Lua::LuaDeathRef<HeaderMapWrapper> wrapper(
      HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; }), true);
  yield_callback_ = [] {};
  start("callMe");
  wrapper.reset();
  EXPECT_THROW_WITH_MESSAGE(coroutine_->resume(0, [] {}), Filters::Common::Lua::LuaException,
                            "[string \"...\"]:5: object used outside of proper scope");
}

// Verify setting the HTTP1 reason phrase
TEST_F(LuaHeaderMapWrapperTest, SetHttp1ReasonPhrase) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:setHttp1ReasonPhrase("Slow Down")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  auto headers = Http::ResponseHeaderMapImpl::create();
  HeaderMapWrapper::create(coroutine_->luaState(), *headers, []() { return true; });
  start("callMe");

  Http::StatefulHeaderKeyFormatterOptRef formatter(headers->formatter());
  EXPECT_EQ(true, formatter.has_value());
  EXPECT_EQ("Slow Down", formatter->getReasonPhrase());
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
    EXPECT_CALL(printer_,
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

// Verify downstream local addresses and downstream direct remote addresses are available from
// stream info wrapper.
TEST_F(LuaStreamInfoWrapperTest, ReturnCurrentDownstreamAddresses) {
  const std::string SCRIPT{R"EOF(
      function callMe(object)
        testPrint(object:downstreamLocalAddress())
        testPrint(object:downstreamDirectRemoteAddress())
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.1", 8000)};
  auto downstream_direct_remote =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance("8.8.8.8", 3000)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(
      downstream_direct_remote);
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint(address->asString()));
  EXPECT_CALL(printer_, testPrint(downstream_direct_remote->asString()));
  start("callMe");
  wrapper.reset();
}

TEST_F(LuaStreamInfoWrapperTest, ReturnRequestedServerName) {
  const std::string SCRIPT{R"EOF(
      function callMe(object)
        testPrint(object:requestedServerName())
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setRequestedServerName("some.sni.io");
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("some.sni.io"));
  start("callMe");
  wrapper.reset();
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

  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("userdata"));
  EXPECT_CALL(printer_, testPrint("bar"));
  EXPECT_CALL(printer_, testPrint("cool"));
  EXPECT_CALL(printer_, testPrint("'foo' 'bar'"));
  EXPECT_CALL(printer_, testPrint("'so' 'cool'"));
  EXPECT_CALL(printer_, testPrint("0"));
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

// Verify that binary values could also be extracted from dynamicMetadata().
TEST_F(LuaStreamInfoWrapperTest, GetDynamicMetadataBinaryData) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local metadata = object:dynamicMetadata():get("envoy.pp")
      local bin_data = metadata["bin_data"]
      local data_length = string.len(metadata["bin_data"])
      for idx = 1, data_length do
        testPrint('Hex Data: ' .. string.format('%x', string.byte(bin_data, idx)))
      end
    end
  )EOF"};

  ProtobufWkt::Value metadata_value;
  constexpr uint8_t buffer[] = {'h', 'e', 0x00, 'l', 'l', 'o'};
  metadata_value.set_string_value(reinterpret_cast<char const*>(buffer), sizeof(buffer));
  ProtobufWkt::Struct metadata;
  metadata.mutable_fields()->insert({"bin_data", metadata_value});

  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  (*stream_info.metadata_.mutable_filter_metadata())["envoy.pp"] = metadata;
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);

  EXPECT_CALL(printer_, testPrint("Hex Data: 68"));          // h (Hex: 68)
  EXPECT_CALL(printer_, testPrint("Hex Data: 65"));          // e (Hex: 65)
  EXPECT_CALL(printer_, testPrint("Hex Data: 0"));           // \0 (Hex: 0)
  EXPECT_CALL(printer_, testPrint("Hex Data: 6c")).Times(2); // l (Hex: 6c)
  EXPECT_CALL(printer_, testPrint("Hex Data: 6f"));          // 0 (Hex: 6f)

  start("callMe");
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("1234"));
  EXPECT_CALL(printer_, testPrint("baz"));
  EXPECT_CALL(printer_, testPrint("true"));
  EXPECT_CALL(printer_, testPrint("cool"));
  EXPECT_CALL(printer_, testPrint("and"));
  EXPECT_CALL(printer_, testPrint("dynamic"));
  EXPECT_CALL(printer_, testPrint("true"));
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
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
        for key, value in pairs(entry) do
          testPrint(string.format("'%s' '%s' '%s'", filter, key, value))
        end
      end

      object:dynamicMetadata():set("envoy.lb", "hello", "envoy")
      object:dynamicMetadata():set("envoy.proxy", "proto", "grpc")

      testPrint("modified")

      for filter, entry in pairs(object:dynamicMetadata()) do
        for key, value in pairs(entry) do
          testPrint(string.format("'%s' '%s' '%s'", filter, key, value))
        end
      end
    end
  )EOF"};

  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  Expectation expect_1 = EXPECT_CALL(printer_, testPrint("'envoy.lb' 'hello' 'world'"));
  Expectation expect_2 = EXPECT_CALL(printer_, testPrint("modified")).After(expect_1);
  EXPECT_CALL(printer_, testPrint("'envoy.proxy' 'proto' 'grpc'")).After(expect_2);
  EXPECT_CALL(printer_, testPrint("'envoy.lb' 'hello' 'envoy'")).After(expect_2);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr);
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
