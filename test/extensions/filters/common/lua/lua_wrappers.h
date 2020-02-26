#pragma once

#include "extensions/filters/common/lua/lua.h"

#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

template <class T> class LuaWrappersTestBase : public testing::Test {
public:
  virtual void setup(const std::string& code) {
    coroutine_.reset();
    state_.reset(new ThreadLocalState(code, tls_));
    state_->registerType<T>();
    coroutine_ = state_->createCoroutine();
    lua_pushlightuserdata(coroutine_->luaState(), this);
    lua_pushcclosure(coroutine_->luaState(), luaTestPrint, 1);
    lua_setglobal(coroutine_->luaState(), "testPrint");
  }

  void start(const std::string& method) {
    coroutine_->start(state_->getGlobalRef(state_->registerGlobal(method)), 1, yield_callback_);
  }

  static int luaTestPrint(lua_State* state) {
    LuaWrappersTestBase* test =
        static_cast<LuaWrappersTestBase*>(lua_touserdata(state, lua_upvalueindex(1)));
    const char* message = luaL_checkstring(state, 1);
    test->testPrint(message);
    return 0;
  }

  MOCK_METHOD(void, testPrint, (const std::string&));

  NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<ThreadLocalState> state_;
  std::function<void()> yield_callback_;
  CoroutinePtr coroutine_;
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
