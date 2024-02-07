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

} // namespace
} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
