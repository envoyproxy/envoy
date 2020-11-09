#pragma once

#include <memory>

#include "extensions/filters/common/lua/lua.h"

#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

// A helper to be called inside the registered closure.
class Printer {
public:
  MOCK_METHOD(void, testPrint, (const std::string&), (const));
};

Printer& getPrinter() { MUTABLE_CONSTRUCT_ON_FIRST_USE(Printer); }

template <class T> class LuaWrappersTestBase : public testing::Test {
public:
  virtual void setup(const std::string& code) {
    coroutine_.reset();
    state_ = std::make_unique<ThreadLocalState>(code, tls_);
    state_->registerType<T>();
    coroutine_ = state_->createCoroutine();
    lua_pushcclosure(coroutine_->luaState(), luaTestPrint, 1);
    lua_setglobal(coroutine_->luaState(), "testPrint");
    testing::Mock::AllowLeak(&printer_);
  }

  void TearDown() override { testing::Mock::VerifyAndClear(&printer_); }

  void start(const std::string& method) {
    coroutine_->start(state_->getGlobalRef(state_->registerGlobal(method)), 1, yield_callback_);
  }

  static int luaTestPrint(lua_State* state) {
    const char* message = luaL_checkstring(state, 1);
    getPrinter().testPrint(message);
    return 0;
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  ThreadLocalStatePtr state_;
  std::function<void()> yield_callback_;
  CoroutinePtr coroutine_;
  Printer& printer_{getPrinter()};
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
