#include <memory>

#include "common/thread_local/thread_local_impl.h"

#include "extensions/filters/common/lua/lua.h"

#include "test/mocks/common.h"
#include "test/mocks/thread_local/mocks.h"
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
  LuaTest() : yield_callback_([this]() { on_yield_.ready(); }) {}

  void setup(const std::string& code) {
    state_ = std::make_unique<ThreadLocalState>(code, tls_);
    state_->registerType<TestObject>();
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  ThreadLocalStatePtr state_;
  std::function<void()> yield_callback_;
  ReadyWatcher on_yield_;
};

// Basic ref counting between coroutines.
TEST_F(LuaTest, CoroutineRefCounting) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);
  EXPECT_EQ(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("not here")));
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMe")));

  // Start a coroutine but do not hold a reference to the object we pass.
  CoroutinePtr cr1(state_->createCoroutine());
  TestObject* object1 = TestObject::create(cr1->luaState()).first;
  cr1->start(state_->getGlobalRef(1), 1, yield_callback_);
  EXPECT_EQ(cr1->state(), Coroutine::State::Finished);
  EXPECT_CALL(*object1, onDestroy());
  lua_gc(cr1->luaState(), LUA_GCCOLLECT, 0);
  cr1.reset();

  // Start a second coroutine but do hold a reference. Do a gc after finish which should not
  // collect it. Then unref and collect and it should be gone.
  CoroutinePtr cr2(state_->createCoroutine());
  LuaRef<TestObject> ref2(TestObject::create(cr2->luaState()), true);
  cr2->start(state_->getGlobalRef(1), 1, yield_callback_);
  EXPECT_EQ(cr2->state(), Coroutine::State::Finished);
  lua_gc(cr2->luaState(), LUA_GCCOLLECT, 0);
  EXPECT_CALL(*ref2.get(), onDestroy());
  ref2.reset();
  lua_gc(cr2->luaState(), LUA_GCCOLLECT, 0);
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
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMe")));

  CoroutinePtr cr(state_->createCoroutine());
  LuaRef<TestObject> ref(TestObject::create(cr->luaState()), true);
  EXPECT_CALL(on_yield_, ready());
  cr->start(state_->getGlobalRef(0), 1, yield_callback_);
  EXPECT_EQ(cr->state(), Coroutine::State::Yielded);

  EXPECT_CALL(*ref.get(), doTestCall(_));
  cr->resume(0, yield_callback_);
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
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMeFirst")));
  EXPECT_NE(LUA_REFNIL, state_->getGlobalRef(state_->registerGlobal("callMeSecond")));

  CoroutinePtr cr1(state_->createCoroutine());
  LuaDeathRef<TestObject> ref(TestObject::create(cr1->luaState()), true);
  EXPECT_CALL(*ref.get(), doTestCall(_));
  EXPECT_CALL(on_yield_, ready());
  cr1->start(state_->getGlobalRef(0), 1, yield_callback_);
  EXPECT_EQ(cr1->state(), Coroutine::State::Yielded);

  ref.markDead();
  CoroutinePtr cr2(state_->createCoroutine());
  EXPECT_THROW_WITH_MESSAGE(cr2->start(state_->getGlobalRef(1), 0, yield_callback_), LuaException,
                            "[string \"...\"]:10: object used outside of proper scope");
  EXPECT_EQ(cr2->state(), Coroutine::State::Finished);

  ref.markLive();
  EXPECT_CALL(*ref.get(), doTestCall(_));
  cr1->resume(0, yield_callback_);
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
  state_ = std::make_unique<ThreadLocalState>(SCRIPT, tls_);
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
