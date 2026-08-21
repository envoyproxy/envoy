#include <memory>

#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/filters/common/lua/lua.h"

#include "test/mocks/common.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {
namespace {

// Setting large alignment requirement here so it fails the UBSAN tests if Lua allocated memory is
// not aligned by Envoy. See https://github.com/envoyproxy/envoy/issues/5551 for details.
class alignas(32) TestObject : public BaseLuaObject<TestObject> {
public:
  ~TestObject() override { onDestroy(); }

  static ExportedFunctions exportedFunctions() { return {{"testCall", static_luaTestCall}}; }

  MOCK_METHOD(int, doTestCall, (lua_State * state));
  MOCK_METHOD(void, onDestroy, ());

private:
  DECLARE_LUA_FUNCTION(TestObject, luaTestCall);
};

int TestObject::luaTestCall(lua_State* state) { return doTestCall(state); }

class LuaTest : public testing::Test {
public:
  LuaTest()
      : yield_callback_([this]() {
          on_yield_.ready();
          return absl::OkStatus();
        }) {}

  void setup(const std::string& code, const PackagePaths& package_paths = {}) {
    absl::Status creation_status = absl::OkStatus();
    state_ = std::make_unique<ThreadLocalState>(code, package_paths, tls_, creation_status);
    THROW_IF_NOT_OK_REF(creation_status);
    state_->registerType<TestObject>();
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  ThreadLocalStatePtr state_;
  YieldCallback yield_callback_;
  ReadyWatcher on_yield_;
  InitializerList initializers_;
};

// Writes a module for a script to require, and returns the search pattern that finds it.
std::string writeTestModule() {
  TestEnvironment::writeStringToFileForTest("lua_test_module.lua", R"EOF(
    local m = {}
    function m.greeting()
      return "hello from the module"
    end
    return m
  )EOF");
  return TestEnvironment::temporaryPath("?.lua");
}

// A configured package path is in place before the code runs, so a top-level require() of a module
// found there resolves. The script errors rather than returning a value, so a failure surfaces as
// a load error out of the constructor.
TEST_F(LuaTest, PackagePathResolvesRequire) {
  const std::string SCRIPT{R"EOF(
    local m = require("lua_test_module")
    if m.greeting() ~= "hello from the module" then
      error("unexpected module contents")
    end
  )EOF"};

  setup(SCRIPT, PackagePaths{writeTestModule(), ""});
}

// Without the package path the same require() fails, and it fails at construction rather than per
// request, because the code is run once to validate it.
TEST_F(LuaTest, PackagePathAbsentFailsToLoad) {
  writeTestModule();
  const std::string SCRIPT{R"EOF(
    local m = require("lua_test_module")
  )EOF"};

  absl::Status creation_status = absl::OkStatus();
  ThreadLocalState state(SCRIPT, PackagePaths{}, tls_, creation_status);
  EXPECT_THAT(creation_status, StatusHelpers::HasStatusMessage(testing::AllOf(
                                   testing::HasSubstr("script load error"),
                                   testing::HasSubstr("module 'lua_test_module' not found"))));
}

// Configured patterns keep the order they were given in, come ahead of the interpreter's own
// search path, and do not replace it.
TEST_F(LuaTest, PackagePathIsPrependedInOrder) {
  const std::string SCRIPT{R"EOF(
    local expected = "/first/?.lua;/second/?.lua;/second/?/init.lua"
    if package.path:sub(1, #expected) ~= expected then
      error("configured patterns are not first: " .. package.path)
    end
    if package.path:sub(#expected + 1, #expected + 1) ~= ";" then
      error("built-in package.path was not kept: " .. package.path)
    end
  )EOF"};

  setup(SCRIPT, PackagePaths{"/first/?.lua;/second/?.lua;/second/?/init.lua", ""});
}

// cpath is prepended the same way. Loading a real C module is out of scope here, so the script
// checks the search path itself.
TEST_F(LuaTest, PackageCpathIsPrepended) {
  const std::string SCRIPT{R"EOF(
    local expected = "/does/not/exist/?.so"
    if package.cpath:sub(1, #expected) ~= expected then
      error("configured pattern is not first: " .. package.cpath)
    end
    if package.cpath:sub(#expected + 1, #expected + 1) ~= ";" then
      error("built-in package.cpath was not kept: " .. package.cpath)
    end
  )EOF"};

  setup(SCRIPT, PackagePaths{"", "/does/not/exist/?.so"});
}

// Basic ref counting between coroutines.
TEST_F(LuaTest, CoroutineRefCounting) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  EXPECT_EQ(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("not here", initializers_)));
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMe", initializers_)));

  // Start a coroutine but do not hold a reference to the object we pass.
  CoroutinePtr cr1(state_->createCoroutine());
  TestObject* object1 = TestObject::create(cr1->luaState()).first;
  EXPECT_TRUE(cr1->start(state_->getGlobalRef(1), 1, yield_callback_).ok());
  EXPECT_EQ(cr1->state(), Coroutine::State::Finished);
  EXPECT_CALL(*object1, onDestroy());
  lua_gc(cr1->luaState(), LUA_GCCOLLECT, 0);
  cr1.reset();

  // Start a second coroutine but do hold a reference. Do a gc after finish which should not
  // collect it. Then unref and collect and it should be gone.
  CoroutinePtr cr2(state_->createCoroutine());
  LuaRef<TestObject> ref2(TestObject::create(cr2->luaState()), true);
  EXPECT_TRUE(cr2->start(state_->getGlobalRef(1), 1, yield_callback_).ok());
  EXPECT_EQ(cr2->state(), Coroutine::State::Finished);
  lua_gc(cr2->luaState(), LUA_GCCOLLECT, 0);
  EXPECT_CALL(*ref2.get(), onDestroy());
  ref2.reset();
  lua_gc(cr2->luaState(), LUA_GCCOLLECT, 0);
}

// Test that we don't crash when empty errors are used (see PR #15471)
TEST_F(LuaTest, EmptyError) {
  const std::string SCRIPT{R"EOF(
    function callMe()
      error()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  const int callMeRef = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));
  EXPECT_NE(LUA_REFNIL, callMeRef);
  CoroutinePtr cr1(state_->createCoroutine());
  EXPECT_THAT(cr1->start(callMeRef, 0, yield_callback_),
              StatusHelpers::HasStatusMessage("unspecified lua error"));
}

// Basic yield/resume functionality.
TEST_F(LuaTest, YieldAndResume) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      coroutine.yield()
      object:testCall()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMe", initializers_)));

  CoroutinePtr cr(state_->createCoroutine());
  LuaRef<TestObject> ref(TestObject::create(cr->luaState()), true);
  EXPECT_CALL(on_yield_, ready());
  EXPECT_TRUE(cr->start(state_->getGlobalRef(0), 1, yield_callback_).ok());
  EXPECT_EQ(cr->state(), Coroutine::State::Yielded);

  EXPECT_CALL(*ref.get(), doTestCall(_));
  EXPECT_TRUE(cr->resume(0, yield_callback_).ok());
  EXPECT_EQ(cr->state(), Coroutine::State::Finished);

  lua_gc(cr->luaState(), LUA_GCCOLLECT, 0);
  EXPECT_CALL(*ref.get(), onDestroy());
  ref.reset();
  lua_gc(cr->luaState(), LUA_GCCOLLECT, 0);
}

// Mark dead/live and ref counting across coroutines.
TEST_F(LuaTest, MarkDead) {
  const std::string SCRIPT{R"EOF(
    function callMeFirst(object)
      global_object = object
      global_object:testCall()
      coroutine.yield()
      global_object:testCall()
    end

    function callMeSecond()
      global_object:testCall()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMeFirst", initializers_)));
  EXPECT_NE(LUA_REFNIL,
            state_->getGlobalRef(state_->registerGlobal("callMeSecond", initializers_)));

  CoroutinePtr cr1(state_->createCoroutine());
  LuaDeathRef<TestObject> ref(TestObject::create(cr1->luaState()), true);
  EXPECT_CALL(*ref.get(), doTestCall(_));
  EXPECT_CALL(on_yield_, ready());
  EXPECT_TRUE(cr1->start(state_->getGlobalRef(0), 1, yield_callback_).ok());
  EXPECT_EQ(cr1->state(), Coroutine::State::Yielded);

  ref.markDead();
  CoroutinePtr cr2(state_->createCoroutine());
  EXPECT_THAT(
      cr2->start(state_->getGlobalRef(1), 0, yield_callback_),
      StatusHelpers::HasStatusMessage("[string \"...\"]:10: object used outside of proper scope"));
  EXPECT_EQ(cr2->state(), Coroutine::State::Finished);

  ref.markLive();
  EXPECT_CALL(*ref.get(), doTestCall(_));
  EXPECT_TRUE(cr1->resume(0, yield_callback_).ok());
  EXPECT_EQ(cr1->state(), Coroutine::State::Finished);

  lua_gc(cr1->luaState(), LUA_GCCOLLECT, 0);
  EXPECT_CALL(*ref.get(), onDestroy());
  ref.reset();
  lua_gc(cr1->luaState(), LUA_GCCOLLECT, 0);
}

class ThreadSafeTest : public testing::Test {
public:
  ThreadSafeTest()
      : api_(Api::createApiForTest()), main_dispatcher_(api_->allocateDispatcher("main")),
        worker_dispatcher_(api_->allocateDispatcher("worker")) {}

  // Use real dispatchers to verify that callback functions can be executed correctly.
  Api::ApiPtr api_;
  Event::DispatcherPtr main_dispatcher_;
  Event::DispatcherPtr worker_dispatcher_;
  ThreadLocal::InstanceImpl tls_;

  std::unique_ptr<ThreadLocalState> state_;
};

// Test whether ThreadLocalState can be safely released.
TEST_F(ThreadSafeTest, StateDestructedBeforeWorkerRun) {
  const std::string SCRIPT{R"EOF(
    function HelloWorld()
      print("Hello World!")
    end
  )EOF"};

  tls_.registerThread(*main_dispatcher_, true);
  EXPECT_EQ(main_dispatcher_.get(), &tls_.dispatcher());
  tls_.registerThread(*worker_dispatcher_, false);

  // Some callback functions waiting to be executed will be added to the dispatcher of the Worker
  // thread. The callback functions in the main thread will be executed directly.
  absl::Status creation_status = absl::OkStatus();
  state_ = std::make_unique<ThreadLocalState>(SCRIPT, PackagePaths{}, tls_, creation_status);
  THROW_IF_NOT_OK_REF(creation_status);
  state_->registerType<TestObject>();

  main_dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Destroy state_.
  state_.reset(nullptr);

  // Start a new worker thread to execute the callback functions in the worker dispatcher.
  Thread::ThreadPtr thread = Thread::threadFactoryForTest().createThread([this]() {
    worker_dispatcher_->run(Event::Dispatcher::RunType::Block);
    // Verify we have the expected dispatcher for the new worker thread.
    EXPECT_EQ(worker_dispatcher_.get(), &tls_.dispatcher());
  });
  thread->join();

  tls_.shutdownGlobalThreading();
  tls_.shutdownThread();
}

} // namespace
} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
