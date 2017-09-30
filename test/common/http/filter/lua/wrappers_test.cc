#include "common/http/filter/lua/wrappers.h"

#include "test/test_common/lua_wrappers.h"
#include "test/test_common/utility.h"

using testing::InSequence;

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

class LuaHeaderMapWrapperTest : public Envoy::Lua::LuaWrappersTestBase<HeaderMapWrapper> {};

// Basic methods test for the header wrapper.
TEST_F(LuaHeaderMapWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:add("HELLO", "WORLD")
      testPrint(object:get("hELLo"))

      object:add("header1", "")
      object:add("header2", "foo")

      object:iterate(
        function(key, value)
          testPrint(string.format("'%s' '%s'", key, value))
        end
      )

      object:remove("header1")
      object:iterate(
        function(key, value)
          testPrint(string.format("'%s' '%s'", key, value))
        end
      )
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(*this, testPrint("WORLD"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header1' ''"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  start("callMe");
}

// Test modifiable methods.
TEST_F(LuaHeaderMapWrapperTest, ModifiableMethods) {
  const std::string SCRIPT{R"EOF(
    function shouldBeOk(object)
      object:get("hELLo")
      object:iterate(
        function(key, value)
        end
      )
    end

    function shouldFailRemove(object)
      object:remove("foo")
    end

    function shouldFailAdd(object)
      object:add("foo")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  start("shouldBeOk");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailRemove"), Envoy::Lua::LuaException,
                            "[string \"...\"]:11: header map can no longer be modified");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailAdd"), Envoy::Lua::LuaException,
                            "[string \"...\"]:15: header map can no longer be modified");
}

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
