#include <memory>
#include <vector>

#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/filters/common/lua/lua.h"

#include "test/mocks/common.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"

using testing::_;
using testing::AnyNumber;
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

  void setup(const std::string& code) {
    absl::Status creation_status = absl::OkStatus();
    state_ = std::make_unique<ThreadLocalState>(code, tls_, creation_status);
    THROW_IF_NOT_OK_REF(creation_status);
    state_->registerType<TestObject>();
  }

  // Runs a one-argument body on a fresh coroutine, expects it to complete, and returns the thread
  // it used. The returned pointer is for identity comparison only -- the coroutine is destroyed on
  // the way out, so the thread is back in the pool by the time the caller sees it.
  //
  // TestObject is a strict mock, so the object handed to the script gets an onDestroy() expectation
  // with no count: when the collector takes it is not the point of any of these tests.
  lua_State* expectCompletes(int function_ref) {
    CoroutinePtr cr(state_->createCoroutine());
    lua_State* thread = cr->luaState();
    TestObject* object = TestObject::create(thread).first;
    EXPECT_CALL(*object, onDestroy()).Times(AnyNumber());
    EXPECT_CALL(*object, doTestCall(_));
    EXPECT_TRUE(cr->start(function_ref, 1, yield_callback_).ok());
    EXPECT_EQ(cr->state(), Coroutine::State::Finished);
    return thread;
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  ThreadLocalStatePtr state_;
  YieldCallback yield_callback_;
  ReadyWatcher on_yield_;
  InitializerList initializers_;
};

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

// A coroutine that finished is reused for the next one. Without this test a pool that never hits
// looks exactly like no pool at all, since the only observable difference is speed.
TEST_F(LuaTest, FinishedCoroutineThreadIsReused) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:testCall()
    end
  )EOF"};

  setup(SCRIPT);
  const int call_me_ref = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));
  EXPECT_NE(LUA_REFNIL, call_me_ref);

  EXPECT_EQ(expectCompletes(call_me_ref), expectCompletes(call_me_ref));
}

// Two coroutines alive at once get two threads, and both come back once they are done: the pool
// has to be a pool rather than one cached thread.
TEST_F(LuaTest, ConcurrentCoroutinesGetDistinctThreads) {
  const std::string SCRIPT{R"EOF(
    function callMe()
    end
  )EOF"};

  setup(SCRIPT);
  const int call_me_ref = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));

  std::vector<lua_State*> first_round;
  {
    CoroutinePtr a(state_->createCoroutine());
    CoroutinePtr b(state_->createCoroutine());
    EXPECT_NE(a->luaState(), b->luaState());
    first_round = {a->luaState(), b->luaState()};
    for (Coroutine* cr : {a.get(), b.get()}) {
      EXPECT_TRUE(cr->start(call_me_ref, 0, yield_callback_).ok());
    }
  }

  CoroutinePtr a(state_->createCoroutine());
  CoroutinePtr b(state_->createCoroutine());
  EXPECT_THAT(first_round, testing::UnorderedElementsAre(a->luaState(), b->luaState()));
}

// A coroutine whose body raised is not resumable, so it must not come back from the pool. Asserted
// through behaviour rather than pointer identity, because a released thread's address can
// legitimately be handed out again by the allocator.
TEST_F(LuaTest, CoroutineAfterAnErrorStartsCleanly) {
  const std::string SCRIPT{R"EOF(
    function raises(object)
      error("boom")
    end
    function callMe(object)
      object:testCall()
    end
  )EOF"};

  setup(SCRIPT);
  const int raises_ref = state_->getGlobalRef(state_->registerGlobal("raises", initializers_));
  const int call_me_ref = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));

  {
    CoroutinePtr cr(state_->createCoroutine());
    TestObject* object = TestObject::create(cr->luaState()).first;
    EXPECT_CALL(*object, onDestroy()).Times(AnyNumber());
    EXPECT_FALSE(cr->start(raises_ref, 1, yield_callback_).ok());
  }

  expectCompletes(call_me_ref);
}

// A coroutine abandoned mid-yield must not come back either: resuming it would continue the body
// it was suspended in rather than start the new one.
TEST_F(LuaTest, CoroutineAfterAnAbandonedYieldStartsCleanly) {
  const std::string SCRIPT{R"EOF(
    function yields(object)
      coroutine.yield()
      object:testCall()
    end
    function callMe(object)
      object:testCall()
    end
  )EOF"};

  setup(SCRIPT);
  const int yields_ref = state_->getGlobalRef(state_->registerGlobal("yields", initializers_));
  const int call_me_ref = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));

  {
    CoroutinePtr cr(state_->createCoroutine());
    TestObject* object = TestObject::create(cr->luaState()).first;
    EXPECT_CALL(*object, onDestroy()).Times(AnyNumber());
    EXPECT_CALL(on_yield_, ready());
    EXPECT_TRUE(cr->start(yields_ref, 1, yield_callback_).ok());
    EXPECT_EQ(cr->state(), Coroutine::State::Yielded);
  }

  expectCompletes(call_me_ref);
}

// Retention is capped, so a burst of concurrent streams does not pin a stack each for the life of
// the worker. Two rounds of more coroutines than the cap: at least MaxSize of the second round's
// threads have to come from the first. That is the direction the allocator cannot fake -- a
// released thread's address can legitimately be handed out again, so asserting the 8 released ones
// are *absent* would be flaky.
TEST_F(LuaTest, PooledThreadsAreCapped) {
  const std::string SCRIPT{R"EOF(
    function callMe()
    end
  )EOF"};

  setup(SCRIPT);
  const int call_me_ref = state_->getGlobalRef(state_->registerGlobal("callMe", initializers_));

  const size_t burst = Coroutine::Pool::MaxSize + 8;
  absl::flat_hash_set<lua_State*> first_round;
  {
    std::vector<CoroutinePtr> live;
    for (size_t i = 0; i < burst; i++) {
      live.push_back(state_->createCoroutine());
      EXPECT_TRUE(live.back()->start(call_me_ref, 0, yield_callback_).ok());
      first_round.insert(live.back()->luaState());
    }
    // All of them are alive at once, so all of them are distinct.
    EXPECT_EQ(burst, first_round.size());
  }

  size_t reused = 0;
  std::vector<CoroutinePtr> live;
  for (size_t i = 0; i < burst; i++) {
    live.push_back(state_->createCoroutine());
    if (first_round.contains(live.back()->luaState())) {
      reused++;
    }
  }
  EXPECT_GE(reused, Coroutine::Pool::MaxSize);
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
  EXPECT_EQ(cr2->state(), Coroutine::State::Errored);

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
  state_ = std::make_unique<ThreadLocalState>(SCRIPT, tls_, creation_status);
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
