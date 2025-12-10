#include "envoy/config/core/v3/base.pb.h"

#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/bool_accessor_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/filters/http/lua/wrappers.h"

#include "test/extensions/filters/common/lua/lua_wrappers.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

using testing::Expectation;
using testing::InSequence;
using testing::Return;
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
    state_->registerType<FilterStateWrapper>();
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
        testPrint(object:downstreamRemoteAddress())
      end
    )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.1", 8000)};
  auto downstream_direct_remote =
      Network::Address::InstanceConstSharedPtr{new Network::Address::Ipv4Instance("8.8.8.8", 3000)};
  auto downstream_remote = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("10.1.2.3", 5000)};
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  stream_info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(
      downstream_direct_remote);
  stream_info.downstream_connection_info_provider_->setRemoteAddress(downstream_remote);
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint(address->asString()));
  EXPECT_CALL(printer_, testPrint(downstream_direct_remote->asString()));
  EXPECT_CALL(printer_, testPrint(downstream_remote->asString()));
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
  const std::string SCRIPT{
      R"EOF(
      function callMe(object)
        testPrint(type(object:dynamicMetadata()))
        object:dynamicMetadata():set("envoy.lb", "foo", "bar")
        object:dynamicMetadata():set("envoy.lb", "so", "cool")
        object:dynamicMetadata():set("envoy.lb", "nothing", nil)

        testPrint(object:dynamicMetadata():get("envoy.lb")["foo"])
        testPrint(object:dynamicMetadata():get("envoy.lb")["so"])
        if object:dynamicMetadata():get("envoy.lb")["nothing"] == nil then
          testPrint("yes")
        end

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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(0, stream_info.dynamicMetadata().filter_metadata_size());
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("userdata"));
  EXPECT_CALL(printer_, testPrint("bar"));
  EXPECT_CALL(printer_, testPrint("cool"));
  EXPECT_CALL(printer_, testPrint("'foo' 'bar'"));
  EXPECT_CALL(printer_, testPrint("'so' 'cool'"));
  EXPECT_CALL(printer_, testPrint("yes"));
  EXPECT_CALL(printer_, testPrint("0"));
  start("callMe");

  EXPECT_EQ(1, stream_info.dynamicMetadata().filter_metadata_size());
  EXPECT_EQ("bar", stream_info.dynamicMetadata()
                       .filter_metadata()
                       .at("envoy.lb")
                       .fields()
                       .at("foo")
                       .string_value());
  EXPECT_TRUE(stream_info.dynamicMetadata()
                  .filter_metadata()
                  .at("envoy.lb")
                  .fields()
                  .at("nothing")
                  .has_null_value());
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

  Protobuf::Value metadata_value;
  constexpr uint8_t buffer[] = {'h', 'e', 0x00, 'l', 'l', 'o'};
  metadata_value.set_string_value(reinterpret_cast<char const*>(buffer), sizeof(buffer));
  Protobuf::Struct metadata;
  metadata.mutable_fields()->insert({"bin_data", metadata_value});

  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
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
  const Protobuf::Struct& meta_foo = stream_info.dynamicMetadata()
                                         .filter_metadata()
                                         .at("envoy.lb")
                                         .fields()
                                         .at("foo")
                                         .struct_value();

  EXPECT_EQ(1234.0, meta_foo.fields().at("x").number_value());
  EXPECT_EQ("baz", meta_foo.fields().at("y").string_value());
  EXPECT_EQ(true, meta_foo.fields().at("z").bool_value());

  const Protobuf::ListValue& meta_so =
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
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

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), Filters::Common::Lua::LuaException,
      "[string \"...\"]:6: cannot create a second iterator before completing the first");
}

// Test for getting the route name
TEST_F(LuaStreamInfoWrapperTest, GetRouteName) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:routeName())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  std::string route_name = "test_route";
  ON_CALL(stream_info, getRouteName()).WillByDefault(testing::ReturnRef(route_name));

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("test_route"));
  start("callMe");
  wrapper.reset();
}

// Test for empty route name
TEST_F(LuaStreamInfoWrapperTest, GetEmptyRouteName) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:routeName())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  std::string empty_route;
  ON_CALL(stream_info, getRouteName()).WillByDefault(testing::ReturnRef(empty_route));

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint(""));
  start("callMe");
  wrapper.reset();
}

TEST_F(LuaStreamInfoWrapperTest, GetVirtualClusterName) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:virtualClusterName())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const absl::optional<std::string> name = absl::make_optional<std::string>("test_virtual_cluster");
  ON_CALL(stream_info, virtualClusterName()).WillByDefault(testing::ReturnRef(name));

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("test_virtual_cluster"));
  start("callMe");
  wrapper.reset();
}

TEST_F(LuaStreamInfoWrapperTest, GetEmptyVirtualClusterName) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:virtualClusterName())
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const absl::optional<std::string> name = absl::nullopt;
  ON_CALL(stream_info, virtualClusterName()).WillByDefault(testing::ReturnRef(name));

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint(""));
  start("callMe");
  wrapper.reset();
}

// Test for dynamicTypedMetadata basic functionality
TEST_F(LuaStreamInfoWrapperTest, GetDynamicTypedMetadataBasic) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local typed_metadata = object:dynamicTypedMetadata("envoy.test.metadata")
      if typed_metadata then
        testPrint("found_metadata")
        testPrint(typed_metadata.fields.test_field.string_value)
      else
        testPrint("no_metadata")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create test typed metadata
  Protobuf::Struct test_struct;
  (*test_struct.mutable_fields())["test_field"].set_string_value("test_value");

  Protobuf::Any any_metadata;
  any_metadata.set_type_url("type.googleapis.com/google.protobuf.Struct");
  any_metadata.PackFrom(test_struct);

  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.test.metadata"] = any_metadata;

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_metadata"));
  EXPECT_CALL(printer_, testPrint("test_value"));
  start("callMe");
  wrapper.reset();
}

// Test for dynamicTypedMetadata with missing metadata
TEST_F(LuaStreamInfoWrapperTest, GetDynamicTypedMetadataMissing) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local typed_metadata = object:dynamicTypedMetadata("envoy.missing.metadata")
      if typed_metadata == nil then
        testPrint("metadata_not_found")
      else
        testPrint("metadata_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("metadata_not_found"));
  start("callMe");
  wrapper.reset();
}

// Test for dynamicTypedMetadata with complex nested structure
TEST_F(LuaStreamInfoWrapperTest, GetDynamicTypedMetadataComplexStructure) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local typed_metadata = object:dynamicTypedMetadata("envoy.complex.metadata")
      if typed_metadata then
        testPrint(typed_metadata.fields.nested.struct_value.fields.inner_field.string_value)
        testPrint(tostring(typed_metadata.fields.bool_field.bool_value))
        testPrint(tostring(typed_metadata.fields.number_field.number_value))
        testPrint(typed_metadata.fields.array_field.list_value.values[1].string_value)
        testPrint(typed_metadata.fields.array_field.list_value.values[2].string_value)
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create complex test metadata
  Protobuf::Struct complex_struct;

  // Add nested structure
  Protobuf::Struct nested_struct;
  (*nested_struct.mutable_fields())["inner_field"].set_string_value("inner_value");
  (*complex_struct.mutable_fields())["nested"].mutable_struct_value()->CopyFrom(nested_struct);

  // Add various field types
  (*complex_struct.mutable_fields())["bool_field"].set_bool_value(true);
  (*complex_struct.mutable_fields())["number_field"].set_number_value(42.5);

  // Add array
  Protobuf::ListValue array_value;
  array_value.add_values()->set_string_value("first");
  array_value.add_values()->set_string_value("second");
  (*complex_struct.mutable_fields())["array_field"].mutable_list_value()->CopyFrom(array_value);

  Protobuf::Any any_metadata;
  any_metadata.set_type_url("type.googleapis.com/google.protobuf.Struct");
  any_metadata.PackFrom(complex_struct);

  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.complex.metadata"] = any_metadata;

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("inner_value"));
  EXPECT_CALL(printer_, testPrint("true"));
  EXPECT_CALL(printer_, testPrint("42.5"));
  EXPECT_CALL(printer_, testPrint("first"));
  EXPECT_CALL(printer_, testPrint("second"));
  start("callMe");
  wrapper.reset();
}

// Test for dynamicTypedMetadata with invalid type URL
TEST_F(LuaStreamInfoWrapperTest, GetDynamicTypedMetadataInvalidTypeUrl) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local typed_metadata = object:dynamicTypedMetadata("envoy.invalid.metadata")
      if typed_metadata == nil then
        testPrint("invalid_type_url_handled")
      else
        testPrint("should_not_reach_here")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create metadata with invalid/unknown type URL
  Protobuf::Any any_metadata;
  any_metadata.set_type_url("type.googleapis.com/invalid.unknown.Type");
  any_metadata.set_value("invalid_data");

  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.invalid.metadata"] = any_metadata;

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("invalid_type_url_handled"));
  start("callMe");
  wrapper.reset();
}

// Test for dynamicTypedMetadata unpack failure handling
TEST_F(LuaStreamInfoWrapperTest, GetDynamicTypedMetadataUnpackFailure) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local typed_metadata = object:dynamicTypedMetadata("envoy.corrupted.metadata")
      if typed_metadata == nil then
        testPrint("unpack_failure_handled")
      else
        testPrint("should_not_reach_here")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create metadata with correct type URL but corrupted data
  Protobuf::Any any_metadata;
  any_metadata.set_type_url("type.googleapis.com/google.protobuf.Struct");
  any_metadata.set_value("corrupted_protobuf_data_that_cannot_be_unpacked");

  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.corrupted.metadata"] =
      any_metadata;

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("unpack_failure_handled"));
  start("callMe");
  wrapper.reset();
}

// Test for iterating over multiple typed metadata entries
TEST_F(LuaStreamInfoWrapperTest, IterateDynamicTypedMetadata) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      -- Test with first metadata entry
      local metadata1 = object:dynamicTypedMetadata("envoy.metadata.one")
      if metadata1 then
        testPrint("found_metadata_one")
        testPrint(metadata1.fields.field_one.string_value)
      end

      -- Test with second metadata entry
      local metadata2 = object:dynamicTypedMetadata("envoy.metadata.two")
      if metadata2 then
        testPrint("found_metadata_two")
        testPrint(metadata2.fields.field_two.string_value)
      end

      -- Test with non-existent entry
      local metadata3 = object:dynamicTypedMetadata("envoy.metadata.nonexistent")
      if metadata3 == nil then
        testPrint("metadata_three_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create first metadata entry
  Protobuf::Struct struct1;
  (*struct1.mutable_fields())["field_one"].set_string_value("value_one");
  Protobuf::Any any1;
  any1.set_type_url("type.googleapis.com/google.protobuf.Struct");
  any1.PackFrom(struct1);
  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.metadata.one"] = any1;

  // Create second metadata entry
  Protobuf::Struct struct2;
  (*struct2.mutable_fields())["field_two"].set_string_value("value_two");
  Protobuf::Any any2;
  any2.set_type_url("type.googleapis.com/google.protobuf.Struct");
  any2.PackFrom(struct2);
  (*stream_info.metadata_.mutable_typed_filter_metadata())["envoy.metadata.two"] = any2;

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_metadata_one"));
  EXPECT_CALL(printer_, testPrint("value_one"));
  EXPECT_CALL(printer_, testPrint("found_metadata_two"));
  EXPECT_CALL(printer_, testPrint("value_two"));
  EXPECT_CALL(printer_, testPrint("metadata_three_not_found"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` basic functionality.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateBasic) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local filter_state_obj = object:filterState():get("test_key")
      if filter_state_obj then
        testPrint("found_filter_state")
        testPrint(filter_state_obj)
      else
        testPrint("no_filter_state")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create a simple string accessor for testing.
  stream_info.filterState()->setData(
      "test_key", std::make_shared<Router::StringAccessorImpl>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_filter_state"));
  EXPECT_CALL(printer_, testPrint("test_value"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` with missing object.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateMissing) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local filter_state_obj = object:filterState():get("missing_key")
      if filter_state_obj == nil then
        testPrint("filter_state_not_found")
      else
        testPrint("filter_state_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("filter_state_not_found"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` with multiple objects.
TEST_F(LuaStreamInfoWrapperTest, GetMultipleFilterStateObjects) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local obj1 = object:filterState():get("key1")
      local obj2 = object:filterState():get("key2")
      local obj3 = object:filterState():get("nonexistent")

      if obj1 then
        testPrint("found_key1")
        testPrint(obj1)
      end

      if obj2 then
        testPrint("found_key2")
        testPrint(obj2)
      end

      if obj3 == nil then
        testPrint("key3_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add multiple filter state objects.
  stream_info.filterState()->setData("key1", std::make_shared<Router::StringAccessorImpl>("value1"),
                                     StreamInfo::FilterState::StateType::ReadOnly,
                                     StreamInfo::FilterState::LifeSpan::FilterChain);

  stream_info.filterState()->setData("key2", std::make_shared<Router::StringAccessorImpl>("value2"),
                                     StreamInfo::FilterState::StateType::ReadOnly,
                                     StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_key1"));
  EXPECT_CALL(printer_, testPrint("value1"));
  EXPECT_CALL(printer_, testPrint("found_key2"));
  EXPECT_CALL(printer_, testPrint("value2"));
  EXPECT_CALL(printer_, testPrint("key3_not_found"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` with numeric accessor.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateNumericAccessor) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local numeric_obj = object:filterState():get("numeric_key")
      if numeric_obj then
        testPrint("found_numeric")
        testPrint(numeric_obj)
        -- Test that it's returned as a string (new behavior)
        if type(numeric_obj) == "string" then
          testPrint("correct_string_type")
        end
      else
        testPrint("numeric_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add numeric filter state object.
  stream_info.filterState()->setData(
      "numeric_key", std::make_shared<StreamInfo::UInt64AccessorImpl>(12345),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_numeric"));
  EXPECT_CALL(printer_, testPrint("12345"));
  EXPECT_CALL(printer_, testPrint("correct_string_type"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` with boolean accessor.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateBooleanAccessor) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local bool_obj = object:filterState():get("bool_key")
      if bool_obj ~= nil then
        testPrint("found_boolean")
        testPrint(bool_obj)
        -- Test that it's returned as a string (new behavior)
        if type(bool_obj) == "string" then
          testPrint("correct_string_type")
        end
      else
        testPrint("boolean_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add boolean filter state object.
  stream_info.filterState()->setData(
      "bool_key", std::make_shared<StreamInfo::BoolAccessorImpl>(true),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_boolean"));
  EXPECT_CALL(printer_, testPrint("true"));
  EXPECT_CALL(printer_, testPrint("correct_string_type"));
  start("callMe");
  wrapper.reset();
}

// Test filter state object that supports field access.
class TestFieldSupportingFilterState : public StreamInfo::FilterState::Object {
public:
  TestFieldSupportingFilterState(std::string base_value) : base_value_(base_value) {}

  absl::optional<std::string> serializeAsString() const override { return base_value_; }

  bool hasFieldSupport() const override { return true; }

  FieldType getField(absl::string_view field_name) const override {
    if (field_name == "string_field") {
      return absl::string_view("field_string_value");
    } else if (field_name == "numeric_field") {
      return int64_t(42);
    } else if (field_name == "base_value") {
      return absl::string_view(base_value_);
    }
    // Return empty variant for non-existent fields.
    return {};
  }

private:
  std::string base_value_;
};

// Test for ``filterState()`` field access with string field.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateFieldAccessString) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local field_value = object:filterState():get("field_key", "string_field")
      if field_value then
        testPrint("found_string_field")
        testPrint(field_value)
        -- Verify it's returned as a string
        if type(field_value) == "string" then
          testPrint("correct_string_type")
        end
      else
        testPrint("string_field_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add field-supporting filter state object.
  stream_info.filterState()->setData(
      "field_key", std::make_shared<TestFieldSupportingFilterState>("base_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_string_field"));
  EXPECT_CALL(printer_, testPrint("field_string_value"));
  EXPECT_CALL(printer_, testPrint("correct_string_type"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` field access with numeric field.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateFieldAccessNumeric) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local field_value = object:filterState():get("field_key", "numeric_field")
      if field_value then
        testPrint("found_numeric_field")
        testPrint(field_value)
        -- Verify it's returned as a number
        if type(field_value) == "number" then
          testPrint("correct_number_type")
        end
      else
        testPrint("numeric_field_not_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add field-supporting filter state object.
  stream_info.filterState()->setData(
      "field_key", std::make_shared<TestFieldSupportingFilterState>("base_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_numeric_field"));
  EXPECT_CALL(printer_, testPrint("42"));
  EXPECT_CALL(printer_, testPrint("correct_number_type"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` field access with non-existent field.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateFieldAccessNonExistent) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local field_value = object:filterState():get("field_key", "nonexistent_field")
      if field_value == nil then
        testPrint("nonexistent_field_returned_nil")
      else
        testPrint("nonexistent_field_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add field-supporting filter state object.
  stream_info.filterState()->setData(
      "field_key", std::make_shared<TestFieldSupportingFilterState>("base_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("nonexistent_field_returned_nil"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` field access on object without field support.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateFieldAccessNoSupport) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local field_value = object:filterState():get("no_field_key", "any_field")
      if field_value == nil then
        testPrint("no_field_support_returned_nil")
      else
        testPrint("no_field_support_found")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add regular string accessor without field support.
  stream_info.filterState()->setData(
      "no_field_key", std::make_shared<Router::StringAccessorImpl>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("no_field_support_returned_nil"));
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` field access fallback to string serialization.
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateFieldAccessFallback) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      -- Test accessing the whole object without field parameter first
      local whole_obj = object:filterState():get("field_key")
      if whole_obj then
        testPrint("found_whole_object")
        testPrint(whole_obj)
      end

      -- Test field access that matches the base_value
      local field_value = object:filterState():get("field_key", "base_value")
      if field_value then
        testPrint("found_base_value_field")
        testPrint(field_value)
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Add field-supporting filter state object.
  stream_info.filterState()->setData(
      "field_key", std::make_shared<TestFieldSupportingFilterState>("test_base"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::FilterChain);

  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("found_whole_object"));
  EXPECT_CALL(printer_, testPrint("test_base")); // String serialization result
  EXPECT_CALL(printer_, testPrint("found_base_value_field"));
  EXPECT_CALL(printer_, testPrint("test_base")); // Field access result
  start("callMe");
  wrapper.reset();
}

// Test for ``filterState()`` with null filter state object (covers lines 398-401).
TEST_F(LuaStreamInfoWrapperTest, GetFilterStateNullObject) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      -- Test accessing non-existent key which will return nullptr from getDataReadOnly
      local null_obj = object:filterState():get("completely_nonexistent_key")
      if null_obj == nil then
        testPrint("null_filter_state_returned_nil")
      else
        testPrint("null_filter_state_found_something")
      end

      -- Test field access on non-existent key
      local null_field = object:filterState():get("completely_nonexistent_key", "any_field")
      if null_field == nil then
        testPrint("null_filter_state_field_returned_nil")
      else
        testPrint("null_filter_state_field_found_something")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Here we are deliberately not adding any filter state data, so ``getDataReadOnly``
  // will return nullptr.
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  EXPECT_CALL(printer_, testPrint("null_filter_state_returned_nil"));
  EXPECT_CALL(printer_, testPrint("null_filter_state_field_returned_nil"));
  start("callMe");
  wrapper.reset();
}

// Test for ``drainConnectionUponCompletion()`` method.
TEST_F(LuaStreamInfoWrapperTest, DrainConnectionUponCompletion) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:drainConnectionUponCompletion()
    end
  )EOF"};

  setup(SCRIPT);

  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http2, test_time_.timeSystem(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::FilterChain);

  // Initially, the connection should not be set to drain.
  EXPECT_FALSE(stream_info.shouldDrainConnectionUponCompletion());

  // Call drainConnectionUponCompletion to drain the connection.
  Filters::Common::Lua::LuaDeathRef<StreamInfoWrapper> wrapper(
      StreamInfoWrapper::create(coroutine_->luaState(), stream_info), true);
  start("callMe");

  EXPECT_TRUE(stream_info.shouldDrainConnectionUponCompletion());

  wrapper.reset();
}

class LuaVirtualHostWrapperTest
    : public Filters::Common::Lua::LuaWrappersTestBase<VirtualHostWrapper> {
public:
  void setup(const std::string& script) override {
    Filters::Common::Lua::LuaWrappersTestBase<VirtualHostWrapper>::setup(script);
    state_->registerType<Filters::Common::Lua::MetadataMapWrapper>();
    state_->registerType<Filters::Common::Lua::MetadataMapIterator>();
  }

  const std::string NO_METADATA_FOUND_SCRIPT{R"EOF(
    function callMe(object)
      for _, _ in pairs(object:metadata()) do
        return
      end
      testPrint("No metadata found")
    end
  )EOF"};
};

// Test that VirtualHostWrapper returns metadata under the current filter configured name.
// This verifies that when virtual host has filter metadata configured under the current filter
// configured name, the wrapper can successfully retrieves and returns it.
TEST_F(LuaVirtualHostWrapperTest, GetFilterMetadataBasic) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local metadata = object:metadata()
      testPrint(metadata:get("foo.bar")["name"])
      testPrint(metadata:get("foo.bar")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a mock virtual host.
  auto virtual_host = std::make_shared<NiceMock<Router::MockVirtualHost>>();
  const Router::VirtualHostConstSharedPtr virtual_host_ptr = virtual_host;

  // Load metadata into the mock virtual host.
  TestUtility::loadFromYaml(METADATA, virtual_host->metadata_);

  // Set up the mock stream info to return the mock virtual host.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, virtualHost()).WillByDefault(ReturnRef(virtual_host_ptr));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<VirtualHostWrapper> wrapper(
      VirtualHostWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"),
      true);

  EXPECT_CALL(printer_, testPrint("foo"));
  EXPECT_CALL(printer_, testPrint("bar"));

  start("callMe");
  wrapper.reset();
}

// Test that VirtualHostWrapper returns an empty metadata object when no metadata exists
// under the current filter configured name.
TEST_F(LuaVirtualHostWrapperTest, GetMetadataNoMetadataUnderFilterName) {
  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.some_filter:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Create a mock virtual host.
  auto virtual_host = std::make_shared<NiceMock<Router::MockVirtualHost>>();
  const Router::VirtualHostConstSharedPtr virtual_host_ptr = virtual_host;

  // Load metadata into the mock virtual host.
  TestUtility::loadFromYaml(METADATA, virtual_host->metadata_);

  // Set up the mock stream info to return the mock virtual host.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, virtualHost()).WillByDefault(ReturnRef(virtual_host_ptr));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<VirtualHostWrapper> wrapper(
      VirtualHostWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"),
      true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

// Test that VirtualHostWrapper returns an empty metadata object when no metadata is configured on
// the virtual host. This verifies that the wrapper correctly handles cases where the virtual host
// has no filter_metadata section, returning an empty metadata object without crashing.
TEST_F(LuaVirtualHostWrapperTest, GetMetadataNoMetadataAtAll) {
  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Create a mock virtual host.
  auto virtual_host = std::make_shared<NiceMock<Router::MockVirtualHost>>();
  const Router::VirtualHostConstSharedPtr virtual_host_ptr = virtual_host;

  // Set up the mock stream info to return the mock virtual host.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, virtualHost()).WillByDefault(ReturnRef(virtual_host_ptr));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<VirtualHostWrapper> wrapper(
      VirtualHostWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"),
      true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

// Test that VirtualHostWrapper returns an empty metadata object when no virtual host matches the
// request authority. This verifies that the wrapper correctly handles cases where the stream info
// does not have a virtual host, returning an empty metadata object without crashing.
TEST_F(LuaVirtualHostWrapperTest, GetMetadataNoVirtualHost) {
  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Set up the mock stream info to return the mock virtual host.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<VirtualHostWrapper> wrapper(
      VirtualHostWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"),
      true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

class LuaRouteWrapperTest : public Filters::Common::Lua::LuaWrappersTestBase<RouteWrapper> {
public:
  void setup(const std::string& script) override {
    Filters::Common::Lua::LuaWrappersTestBase<RouteWrapper>::setup(script);
    state_->registerType<Filters::Common::Lua::MetadataMapWrapper>();
    state_->registerType<Filters::Common::Lua::MetadataMapIterator>();
  }

  const std::string NO_METADATA_FOUND_SCRIPT{R"EOF(
    function callMe(object)
      for _, _ in pairs(object:metadata()) do
        return
      end
      testPrint("No metadata found")
    end
  )EOF"};
};

// Test that RouteWrapper returns metadata under the current filter configured name.
// This verifies that when route has filter metadata configured under the current filter
// configured name, the wrapper can successfully retrieves and returns it.
TEST_F(LuaRouteWrapperTest, GetFilterMetadataBasic) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      local metadata = object:metadata()
      testPrint(metadata:get("foo.bar")["name"])
      testPrint(metadata:get("foo.bar")["prop"])
    end
  )EOF"};

  const std::string METADATA{R"EOF(
    filter_metadata:
      lua-filter-config-name:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  // Create a mock route and load metadata into it.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  TestUtility::loadFromYaml(METADATA, route->metadata_);

  // Set up the mock stream info to return the mock route.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, route()).WillByDefault(Return(route));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<RouteWrapper> wrapper(
      RouteWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"), true);

  EXPECT_CALL(printer_, testPrint("foo"));
  EXPECT_CALL(printer_, testPrint("bar"));

  start("callMe");
  wrapper.reset();
}

// Test that RouteWrapper returns an empty metadata object when no metadata exists
// under the current filter configured name.
TEST_F(LuaRouteWrapperTest, GetMetadataNoMetadataUnderFilterName) {
  const std::string METADATA{R"EOF(
    filter_metadata:
      envoy.some_filter:
        foo.bar:
          name: foo
          prop: bar
  )EOF"};

  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Create a mock route and load metadata into it.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();
  TestUtility::loadFromYaml(METADATA, route->metadata_);

  // Set up the mock stream info to return the mock route.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, route()).WillByDefault(Return(route));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<RouteWrapper> wrapper(
      RouteWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"), true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

// Test that RouteWrapper returns an empty metadata object when no metadata is configured on
// the route. This verifies that the wrapper correctly handles cases where the route
// has no filter_metadata section, returning an empty metadata object without crashing.
TEST_F(LuaRouteWrapperTest, GetMetadataNoMetadataAtAll) {
  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Create a mock route but DO NOT load metadata into it.
  auto route = std::make_shared<NiceMock<Router::MockRoute>>();

  // Set up the mock stream info to return the mock route.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, route()).WillByDefault(Return(route));

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<RouteWrapper> wrapper(
      RouteWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"), true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

// Test that RouteWrapper returns an empty metadata object when no route matches the
// request. This verifies that the wrapper correctly handles cases where the stream info
// does not have a route, returning an empty metadata object without crashing.
TEST_F(LuaRouteWrapperTest, GetMetadataNoRoute) {
  InSequence s;
  setup(NO_METADATA_FOUND_SCRIPT);

  // Set up the mock stream info but DO NOT config it to return a valid route.
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Set up wrapper with the mock stream info.
  Filters::Common::Lua::LuaDeathRef<RouteWrapper> wrapper(
      RouteWrapper::create(coroutine_->luaState(), stream_info, "lua-filter-config-name"), true);

  EXPECT_CALL(printer_, testPrint("No metadata found"));

  start("callMe");
  wrapper.reset();
}

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
