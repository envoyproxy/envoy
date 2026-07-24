#include <optional>
#include <type_traits>

#include "source/common/common/callbacks_handler.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace {

struct TestCallbacks {
  int id{0};
};

using Handler = CallbacksHandler<TestCallbacks>;
using Wrapper = Handler::CallbacksWrapper;

// These are move-only types: move enabled (to keep them on the stack), copy deleted (it would
// alias the peer's single back-reference).
static_assert(std::is_move_constructible_v<Handler>);
static_assert(std::is_move_assignable_v<Handler>);
static_assert(!std::is_copy_constructible_v<Handler>);
static_assert(!std::is_copy_assignable_v<Handler>);
static_assert(std::is_move_constructible_v<Wrapper>);
static_assert(std::is_move_assignable_v<Wrapper>);
static_assert(!std::is_copy_constructible_v<Wrapper>);
static_assert(!std::is_copy_assignable_v<Wrapper>);

// Construction wires both directions and exposes the callbacks.
TEST(CallbacksHandlerTest, BasicLinkAndAccess) {
  TestCallbacks cbs{42};
  Handler handler;
  Wrapper wrapper(handler, cbs);

  ASSERT_TRUE(wrapper.callbacks().has_value());
  EXPECT_EQ(&cbs, wrapper.callbacks().ptr());
  EXPECT_EQ(42, wrapper.callbacks()->id);
}

// Destroying the handler first invalidates the wrapper; the wrapper survives with empty
// callbacks().
TEST(CallbacksHandlerTest, DestroyHandlerFirst) {
  TestCallbacks cbs;
  Wrapper* wrapper_ptr = nullptr;
  std::optional<Wrapper> wrapper;
  {
    Handler handler;
    wrapper.emplace(handler, cbs);
    wrapper_ptr = &wrapper.value();
    EXPECT_TRUE(wrapper_ptr->callbacks().has_value());
  }
  // Handler gone: wrapper must now be invalid (and not dangling).
  EXPECT_FALSE(wrapper_ptr->callbacks().has_value());
  EXPECT_FALSE(wrapper_ptr->callbacks().has_value());
}

// Destroying the wrapper first leaves the handler safe to reset and destroy.
TEST(CallbacksHandlerTest, DestroyWrapperFirst) {
  TestCallbacks cbs;
  Handler handler;
  {
    Wrapper wrapper(handler, cbs);
    EXPECT_TRUE(wrapper.callbacks().has_value());
  }
  // Wrapper gone: handler is unlinked. reset() and destruction must be safe no-ops.
  handler.reset();
  SUCCEED();
}

// Explicit reset() from the handler side invalidates the wrapper.
TEST(CallbacksHandlerTest, ExplicitHandlerReset) {
  TestCallbacks cbs;
  Handler handler;
  Wrapper wrapper(handler, cbs);

  handler.reset();
  EXPECT_FALSE(wrapper.callbacks().has_value());
  // Double reset / destruction are safe.
  handler.reset();
}

// Explicit reset() from the wrapper side unlinks the handler.
TEST(CallbacksHandlerTest, ExplicitWrapperReset) {
  TestCallbacks cbs;
  Handler handler;
  Wrapper wrapper(handler, cbs);

  wrapper.reset();
  EXPECT_FALSE(wrapper.callbacks().has_value());
  // Handler no longer points at the wrapper; destroying it is a no-op.
}

// Move-constructing the wrapper transfers the link to the new object and empties the source.
TEST(CallbacksHandlerTest, MoveConstructWrapper) {
  TestCallbacks cbs{7};
  std::optional<Handler> handler;
  handler.emplace();

  Wrapper src(*handler, cbs);
  Wrapper dst(std::move(src));

  // NOLINTNEXTLINE(bugprone-use-after-move) -- intentionally asserting the moved-from state.
  EXPECT_FALSE(src.callbacks().has_value());
  EXPECT_TRUE(dst.callbacks().has_value());
  EXPECT_EQ(&cbs, dst.callbacks().ptr());

  // The handler is now linked to dst: destroying it invalidates dst, not the empty src.
  handler.reset();
  EXPECT_FALSE(dst.callbacks().has_value());
}

// Move-constructing the handler transfers the back-link so the wrapper still tracks it.
TEST(CallbacksHandlerTest, MoveConstructHandler) {
  TestCallbacks cbs;
  std::optional<Wrapper> wrapper;

  Handler src;
  // Build the wrapper against src, then move src into dst.
  wrapper.emplace(src, cbs);
  Handler dst(std::move(src));

  EXPECT_TRUE(wrapper->callbacks().has_value());
  // Destroying the moved-from src must NOT invalidate the wrapper (the link moved to dst).
  {
    Handler sink(std::move(dst)); // move again to prove the link keeps following the handler.
    EXPECT_TRUE(wrapper->callbacks().has_value());
  }
  // sink destroyed -> wrapper invalidated.
  EXPECT_FALSE(wrapper->callbacks().has_value());
}

// Self move-assignment is a no-op and keeps the link intact.
TEST(CallbacksHandlerTest, MoveAssignWrapperSelf) {
  TestCallbacks cbs{99};
  Handler handler;
  Wrapper wrapper(handler, cbs);

  Wrapper* alias = &wrapper;
  wrapper = std::move(*alias); // self move-assign via alias to dodge the self-move warning.

  EXPECT_TRUE(wrapper.callbacks().has_value());
  EXPECT_EQ(99, wrapper.callbacks()->id);
}

// Move-assigning over a live link detaches the old handler and adopts the source's link.
TEST(CallbacksHandlerTest, MoveAssignWrapperOverLiveLink) {
  TestCallbacks cbs1{1};
  TestCallbacks cbs2{2};
  std::optional<Handler> h1;
  std::optional<Handler> h2;
  h1.emplace();
  h2.emplace();

  Wrapper w1(*h1, cbs1);
  Wrapper w2(*h2, cbs2);

  w1 = std::move(w2);

  ASSERT_TRUE(w1.callbacks().has_value());
  EXPECT_EQ(&cbs2, w1.callbacks().ptr());
  EXPECT_EQ(2, w1.callbacks()->id);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_FALSE(w2.callbacks().has_value());

  // h1 was detached: destroying it must not touch w1.
  h1.reset();
  EXPECT_TRUE(w1.callbacks().has_value());

  // Destroying h2 invalidates w1.
  h2.reset();
  EXPECT_FALSE(w1.callbacks().has_value());
}

// Symmetric move-assign test from the handler side.
TEST(CallbacksHandlerTest, MoveAssignHandlerOverLiveLink) {
  TestCallbacks cbs1{1};
  TestCallbacks cbs2{2};
  std::optional<Wrapper> w1;
  std::optional<Wrapper> w2;

  Handler h1;
  Handler h2;
  w1.emplace(h1, cbs1);
  w2.emplace(h2, cbs2);

  h1 = std::move(h2);

  // h1 now owns the link to w2; w1's handler (old h1) has been detached -> w1 invalid.
  EXPECT_FALSE(w1->callbacks().has_value());
  EXPECT_TRUE(w2->callbacks().has_value());

  // Destroying h1 (which now drives w2) invalidates w2.
  h1.reset();
  EXPECT_FALSE(w2->callbacks().has_value());
}

// A subclass can observe peer-initiated teardown by overriding onPeerReset(), and active
// teardown by overriding reset(). Stays move-able because it declares no dtor/copy/move.
class ObservingHandler : public Handler {
public:
  void reset() override {
    ++reset_calls_;
    Handler::reset();
  }
  int reset_calls_{0};
  int peer_reset_calls_{0};

protected:
  void onPeerReset() override {
    ++peer_reset_calls_;
    Handler::onPeerReset();
  }
};

TEST(CallbacksHandlerTest, SubclassObservesTeardown) {
  TestCallbacks cbs;
  ObservingHandler handler;
  {
    Wrapper wrapper(handler, cbs);
    EXPECT_TRUE(wrapper.callbacks().has_value());
    // Wrapper destroyed here -> handler->onPeerReset() dispatches to the override.
  }
  EXPECT_EQ(1, handler.peer_reset_calls_);
  EXPECT_EQ(0, handler.reset_calls_);

  // Explicit reset() dispatches to the override too.
  ObservingHandler handler2;
  TestCallbacks cbs2;
  Wrapper wrapper2(handler2, cbs2);
  handler2.reset();
  EXPECT_EQ(1, handler2.reset_calls_);
  EXPECT_FALSE(wrapper2.callbacks().has_value());
}

// Attaching a second wrapper to a handler that already has one trips the ASSERT (debug builds).
TEST(CallbacksHandlerDeathTest, DoubleAttachAsserts) {
  TestCallbacks cbs1;
  TestCallbacks cbs2;
  Handler handler;
  Wrapper wrapper(handler, cbs1);

  EXPECT_DEBUG_DEATH({ Wrapper second(handler, cbs2); }, "already has a wrapper");
}

} // namespace
} // namespace Common
} // namespace Envoy
