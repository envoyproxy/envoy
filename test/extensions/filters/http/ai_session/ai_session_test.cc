// E2E tests for the AiSession filter chain.
//
// Test organisation mirrors mcp_filter_test.cc (unit) and
// mcp_filter_integration_test.cc (stack-level):
//
//   Part 1 – Unit tests: AiFilterChain driven directly (no HTTP or parser).
//   Part 2 – Stack tests: JsonRpcParser → AiFilterChain (raw bytes → events).
//   Part 3 – Session manager tests: AiSessionManager + full filter pipeline.

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_session/ai_filter.h"
#include "source/extensions/filters/http/ai_session/ai_filter_chain.h"
#include "source/extensions/filters/http/ai_session/ai_session.h"
#include "source/extensions/filters/http/ai_session/ai_session_manager.h"
#include "source/extensions/filters/http/ai_session/example_filters.h"
#include "source/extensions/filters/http/json_rpc/json_rpc_parser.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// =============================================================================
// Test utilities
// =============================================================================

// RecordingFilter — records every event delivered to it.
// Place at the END of the filter list to observe what the preceding filters
// allowed through. Analogy: an access-log filter in an HTTP chain.
class RecordingFilter : public AiStreamFilter {
public:
  struct Event {
    enum class Type { Begin, Method, Id, Params, Complete, Error } type;
    std::string method;      // Method events
    nlohmann::json id;       // Id events
    nlohmann::json params;   // Params events
    std::string error;       // Error events
  };

  void setCallbacks(AiStreamFilterCallbacks& cb) override { callbacks_ = &cb; }

  AiFilterStatus onJsonRpcBegin() override {
    events_.push_back({Event::Type::Begin, {}, {}, {}, {}});
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onMethod(absl::string_view method) override {
    events_.push_back({Event::Type::Method, std::string(method), {}, {}, {}});
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onId(const nlohmann::json& id) override {
    events_.push_back({Event::Type::Id, {}, id, {}, {}});
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onParams(const nlohmann::json& params) override {
    events_.push_back({Event::Type::Params, {}, {}, params, {}});
    return AiFilterStatus::Continue;
  }
  AiFilterStatus onJsonRpcComplete() override {
    events_.push_back({Event::Type::Complete, {}, {}, {}, {}});
    return AiFilterStatus::Continue;
  }
  void onError(absl::string_view msg) override {
    events_.push_back({Event::Type::Error, {}, {}, {}, std::string(msg)});
  }

  const std::vector<Event>& events() const { return events_; }

  std::vector<Event::Type> eventTypes() const {
    std::vector<Event::Type> types;
    for (const auto& e : events_) types.push_back(e.type);
    return types;
  }

  bool sawMethod(absl::string_view m) const {
    for (const auto& e : events_)
      if (e.type == Event::Type::Method && e.method == m) return true;
    return false;
  }

private:
  AiStreamFilterCallbacks* callbacks_{nullptr};
  std::vector<Event> events_;
};

// AsyncStoppingFilter — stops on onMethod; the test drives async resume via
// the stored callbacks pointer. Models an auth filter performing an async RPC.
class AsyncStoppingFilter : public AiStreamFilter {
public:
  void setCallbacks(AiStreamFilterCallbacks& cb) override { callbacks_ = &cb; }

  AiFilterStatus onJsonRpcBegin() override { return AiFilterStatus::Continue; }

  AiFilterStatus onMethod(absl::string_view m) override {
    seen_method_ = std::string(m);
    if (!resumed_) {
      // First time: stop and wait for the test to call resume().
      return AiFilterStatus::StopIteration;
    }
    return AiFilterStatus::Continue;
  }

  AiFilterStatus onId(const nlohmann::json&) override { return AiFilterStatus::Continue; }
  AiFilterStatus onParams(const nlohmann::json&) override { return AiFilterStatus::Continue; }
  AiFilterStatus onJsonRpcComplete() override { return AiFilterStatus::Continue; }
  void onError(absl::string_view) override {}

  void resume() {
    resumed_ = true;
    callbacks_->continueProcessing();
  }

  const std::string& seenMethod() const { return seen_method_; }

private:
  AiStreamFilterCallbacks* callbacks_{nullptr};
  std::string seen_method_;
  bool resumed_{false};
};

// =============================================================================
// Fixture helpers
// =============================================================================

// Build an AiFilterChain from a variadic list of filter unique_ptrs.
template <typename... Filters>
std::unique_ptr<AiFilterChain> makeChain(AiSession& session, Filters&&... filters) {
  std::vector<std::unique_ptr<AiStreamFilter>> list;
  (list.push_back(std::forward<Filters>(filters)), ...);
  return std::make_unique<AiFilterChain>(session, std::move(list));
}

// Feed a raw JSON string through JsonRpcParser into a decoder.
absl::Status feedJson(JsonRpc::JsonRpcDecoder& decoder, absl::string_view json) {
  Buffer::OwnedImpl buf(json);
  JsonRpc::JsonRpcParser parser(decoder);
  if (auto s = parser.parse(buf); !s.ok()) return s;
  return parser.finishParse();
}

// =============================================================================
// Part 1 – AiFilterChain unit tests
// =============================================================================

class AiFilterChainTest : public testing::Test {
protected:
  AiSession session_{"unit-session"};
};

// All events flow through when no filter stops.
TEST_F(AiFilterChainTest, AllEventsContinue) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  chain->onJsonRpcBegin();
  chain->onMethod("tools/call");
  chain->onId(nlohmann::json(42));
  chain->onParams(nlohmann::json::object({{"name", "search"}}));
  chain->onJsonRpcComplete();

  EXPECT_THAT(rec->eventTypes(),
              ElementsAre(RecordingFilter::Event::Type::Begin,
                          RecordingFilter::Event::Type::Method,
                          RecordingFilter::Event::Type::Id,
                          RecordingFilter::Event::Type::Params,
                          RecordingFilter::Event::Type::Complete));
  EXPECT_TRUE(chain->isRunning());
}

// StopIteration on onMethod queues subsequent events.
// When the stopped filter resumes, all queued events are delivered in order.
TEST_F(AiFilterChainTest, StopOnMethodQueuesSubsequentEvents) {
  auto* stopper = new AsyncStoppingFilter;
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_,
                         std::unique_ptr<AiStreamFilter>(stopper),
                         std::unique_ptr<AiStreamFilter>(rec));

  chain->onJsonRpcBegin();
  chain->onMethod("tools/call");

  // Chain is stopped at stopper; rec has not seen onMethod yet.
  EXPECT_TRUE(chain->isStopped());
  EXPECT_THAT(rec->events(), SizeIs(1));
  EXPECT_EQ(RecordingFilter::Event::Type::Begin, rec->events()[0].type);

  // Events that arrive while stopped are queued.
  chain->onId(nlohmann::json(1));
  chain->onParams(nlohmann::json::object({{"name", "x"}}));
  chain->onJsonRpcComplete();

  EXPECT_EQ(3u, chain->pendingEventCount()); // id, params, complete
  EXPECT_THAT(rec->events(), SizeIs(1));     // rec only saw Begin

  // Async resume: stopper finishes its work and calls continueProcessing().
  stopper->resume();

  // Now rec should have received ALL events in document order.
  EXPECT_TRUE(chain->isRunning());
  EXPECT_THAT(rec->eventTypes(),
              ElementsAre(RecordingFilter::Event::Type::Begin,   // before stop
                          RecordingFilter::Event::Type::Method,  // resumed
                          RecordingFilter::Event::Type::Id,      // drained
                          RecordingFilter::Event::Type::Params,
                          RecordingFilter::Event::Type::Complete));
  EXPECT_EQ("tools/call", stopper->seenMethod());
}

// sendJsonRpcError → chain enters Error state; subsequent events are silently dropped.
// This tests the bug fix: Error must not be overwritten by the Stopped state.
TEST_F(AiFilterChainTest, SendJsonRpcErrorSetsErrorStateAndBlocksSubsequentEvents) {
  // A filter that sends an error on onMethod.
  class ErrorOnMethodFilter : public AiStreamFilter {
  public:
    void setCallbacks(AiStreamFilterCallbacks& cb) override { cb_ = &cb; }
    AiFilterStatus onJsonRpcBegin() override { return AiFilterStatus::Continue; }
    AiFilterStatus onMethod(absl::string_view) override {
      cb_->sendJsonRpcError(-32001, "Unauthenticated");
      return AiFilterStatus::StopIteration;
    }
    AiFilterStatus onId(const nlohmann::json&) override { return AiFilterStatus::Continue; }
    AiFilterStatus onParams(const nlohmann::json&) override { return AiFilterStatus::Continue; }
    AiFilterStatus onJsonRpcComplete() override { return AiFilterStatus::Continue; }
    void onError(absl::string_view) override {}
  private:
    AiStreamFilterCallbacks* cb_{nullptr};
  };

  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_,
                         std::make_unique<ErrorOnMethodFilter>(),
                         std::unique_ptr<AiStreamFilter>(rec));

  chain->onJsonRpcBegin();
  chain->onMethod("tools/call");

  // Chain must be in Error, not Stopped (the bug fix).
  EXPECT_TRUE(chain->hasError());
  EXPECT_FALSE(chain->isStopped());

  // Subsequent events are swallowed; rec sees nothing after begin.
  chain->onId(nlohmann::json(1));
  chain->onParams(nlohmann::json::object());
  chain->onJsonRpcComplete();

  // rec saw Begin (before the error filter), but nothing after onMethod.
  std::vector<RecordingFilter::Event::Type> types = rec->eventTypes();
  EXPECT_THAT(types, ElementsAre(RecordingFilter::Event::Type::Begin));
}

// onError (parse error) is propagated to all filters.
TEST_F(AiFilterChainTest, ParseErrorPropagatedToAllFilters) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  chain->onJsonRpcBegin();
  chain->onError("unexpected token '}'");

  EXPECT_TRUE(chain->hasError());
  EXPECT_THAT(rec->eventTypes(),
              ElementsAre(RecordingFilter::Event::Type::Begin,
                          RecordingFilter::Event::Type::Error));
  EXPECT_EQ("unexpected token '}'", rec->events().back().error);
}

// =============================================================================
// Part 2 – Stack tests: JsonRpcParser → AiFilterChain (raw bytes → events)
// =============================================================================

class JsonRpcStackTest : public testing::Test {
protected:
  AiSession session_{"stack-session"};
};

// Full tools/call message fires events in document order.
TEST_F(JsonRpcStackTest, ToolsCallMessageFiresAllEvents) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  constexpr absl::string_view body =
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"search","arguments":{"q":"envoy"}}})";
  ASSERT_TRUE(feedJson(*chain, body).ok());

  EXPECT_THAT(rec->eventTypes(),
              ElementsAre(RecordingFilter::Event::Type::Begin,
                          RecordingFilter::Event::Type::Id,
                          RecordingFilter::Event::Type::Method,
                          RecordingFilter::Event::Type::Params,
                          RecordingFilter::Event::Type::Complete));

  // Verify field values.
  bool found_method = false;
  bool found_id = false;
  bool found_params = false;
  for (const auto& ev : rec->events()) {
    if (ev.type == RecordingFilter::Event::Type::Method) {
      EXPECT_EQ("tools/call", ev.method);
      found_method = true;
    } else if (ev.type == RecordingFilter::Event::Type::Id) {
      EXPECT_EQ(7, ev.id.get<int>());
      found_id = true;
    } else if (ev.type == RecordingFilter::Event::Type::Params) {
      EXPECT_EQ("search", ev.params.at("name").get<std::string>());
      found_params = true;
    }
  }
  EXPECT_TRUE(found_method);
  EXPECT_TRUE(found_id);
  EXPECT_TRUE(found_params);
}

// Notification (no "id" field): onId must NOT fire.
TEST_F(JsonRpcStackTest, NotificationHasNoId) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})").ok());

  for (const auto& ev : rec->events()) {
    EXPECT_NE(RecordingFilter::Event::Type::Id, ev.type)
        << "onId should not fire for notifications";
  }
  EXPECT_TRUE(rec->sawMethod("notifications/initialized"));
}

// Malformed JSON → onError fires, chain enters Error state.
TEST_F(JsonRpcStackTest, MalformedJsonFiresOnError) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  EXPECT_FALSE(feedJson(*chain, R"({"jsonrpc":"2.0","id":1,"method":)").ok()); // truncated

  EXPECT_TRUE(chain->hasError());
  bool saw_error = false;
  for (const auto& ev : rec->events())
    if (ev.type == RecordingFilter::Event::Type::Error) saw_error = true;
  EXPECT_TRUE(saw_error);
}

// onMethod fires BEFORE onParams, even when method appears first in the JSON.
TEST_F(JsonRpcStackTest, MethodFiresBeforeParams) {
  auto* rec = new RecordingFilter;
  auto chain = makeChain(session_, std::unique_ptr<AiStreamFilter>(rec));

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","method":"tools/call","params":{"large":"value"}})").ok());

  int method_idx = -1, params_idx = -1;
  for (int i = 0; i < static_cast<int>(rec->events().size()); ++i) {
    if (rec->events()[i].type == RecordingFilter::Event::Type::Method) method_idx = i;
    if (rec->events()[i].type == RecordingFilter::Event::Type::Params) params_idx = i;
  }
  ASSERT_GE(method_idx, 0);
  ASSERT_GE(params_idx, 0);
  EXPECT_LT(method_idx, params_idx) << "onMethod must fire before onParams";
}

// =============================================================================
// Part 3 – Example filter tests (McpAuthFilter, McpInitFilter, McpContextFilter)
// =============================================================================

class ExampleFilterTest : public testing::Test {
protected:
  AiSession session_{"example-session"};

  void authenticate(absl::string_view principal = "user") {
    session_.setIdentity(std::string(principal));
  }

  void initSession() {
    session_.markInitialized(nlohmann::json::object({{"sampling", {}}}), "2024-11-05");
  }

  // Build chain: [McpAuthFilter][McpInitFilter][McpContextFilter][RecordingFilter]
  // Returns the RecordingFilter pointer (owned by the chain).
  std::pair<std::unique_ptr<AiFilterChain>, RecordingFilter*> makeExampleChain() {
    auto* rec = new RecordingFilter;
    auto chain = makeChain(session_,
                           std::make_unique<McpAuthFilter>(),
                           std::make_unique<McpInitFilter>(),
                           std::make_unique<McpContextFilter>(),
                           std::unique_ptr<AiStreamFilter>(rec));
    return {std::move(chain), rec};
  }
};

// Unauthenticated request → McpAuthFilter sends error; no downstream filters see onMethod.
TEST_F(ExampleFilterTest, UnauthenticatedToolCallIsRejected) {
  auto [chain, rec] = makeExampleChain();
  // session_ has no identity set.

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"x"}})").ok());

  EXPECT_TRUE(chain->hasError());
  EXPECT_FALSE(rec->sawMethod("tools/call"))
      << "RecordingFilter must not see onMethod when auth rejects";
}

// Authenticated but uninitialized → McpAuthFilter passes, McpInitFilter rejects.
TEST_F(ExampleFilterTest, UnitializedSessionIsRejectedAfterAuth) {
  authenticate(); // user is known but session.isInitialized() == false
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{}})").ok());

  EXPECT_TRUE(chain->hasError());
  EXPECT_FALSE(rec->sawMethod("tools/call"));
}

// initialize request succeeds and marks session as initialized.
TEST_F(ExampleFilterTest, InitializeRequestMarksSessionInitialized) {
  authenticate();
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{"sampling":{}}}})").ok());

  EXPECT_TRUE(chain->isRunning());
  EXPECT_TRUE(session_.isInitialized());
  EXPECT_TRUE(rec->sawMethod("initialize"));
}

// After initialization, a tools/call request succeeds end to end.
TEST_F(ExampleFilterTest, AuthenticatedInitializedToolCallSucceeds) {
  authenticate();
  initSession();
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"search","arguments":{"q":"test"}}})").ok());

  EXPECT_TRUE(chain->isRunning());
  EXPECT_TRUE(rec->sawMethod("tools/call"));
  EXPECT_THAT(rec->eventTypes(),
              ElementsAre(RecordingFilter::Event::Type::Begin,
                          RecordingFilter::Event::Type::Id,
                          RecordingFilter::Event::Type::Method,
                          RecordingFilter::Event::Type::Params,
                          RecordingFilter::Event::Type::Complete));
}

// Admin method rejected for non-admin principal.
TEST_F(ExampleFilterTest, AdminMethodRejectedForNonAdminUser) {
  authenticate("user"); // not admin
  initSession();
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":4,"method":"admin/reset","params":{}})").ok());

  EXPECT_TRUE(chain->hasError());
  EXPECT_FALSE(rec->sawMethod("admin/reset"));
}

// Admin method allowed for admin principal.
TEST_F(ExampleFilterTest, AdminMethodAllowedForAdminPrincipal) {
  authenticate("admin");
  initSession();
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","id":5,"method":"admin/reset","params":{}})").ok());

  EXPECT_TRUE(chain->isRunning());
  EXPECT_TRUE(rec->sawMethod("admin/reset"));
}

// McpContextFilter accumulates context across multiple requests on the same session.
TEST_F(ExampleFilterTest, ContextAccumulatesAcrossRequests) {
  authenticate();
  initSession();

  EXPECT_EQ(0u, session_.context().size());

  // Request 1.
  {
    auto [chain, rec] = makeExampleChain();
    ASSERT_TRUE(feedJson(*chain,
                         R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"a"}})").ok());
    EXPECT_EQ(1u, session_.context().size());
  }

  // Request 2 — chain recreated (new request, same session).
  {
    auto [chain, rec] = makeExampleChain();
    ASSERT_TRUE(feedJson(*chain,
                         R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"b"}})").ok());
    EXPECT_EQ(2u, session_.context().size());
  }

  EXPECT_EQ("tools/call", session_.context()[0].at("method").get<std::string>());
  EXPECT_EQ("tools/call", session_.context()[1].at("method").get<std::string>());
}

// notifications/initialized does not add to context (not a tool call or resource read).
TEST_F(ExampleFilterTest, InitializedNotificationDoesNotAddContext) {
  authenticate();
  initSession();
  auto [chain, rec] = makeExampleChain();

  ASSERT_TRUE(feedJson(*chain,
                       R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})").ok());

  EXPECT_EQ(0u, session_.context().size());
}

// =============================================================================
// Part 4 – AiSessionManager tests
// =============================================================================

class AiSessionManagerTest : public testing::Test {
protected:
  AiSessionManagerTest() {
    manager_ = std::make_unique<AiSessionManager>(std::vector<AiFilterFactory>{
        []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpAuthFilter>(); },
        []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpInitFilter>(); },
        []() -> std::unique_ptr<AiStreamFilter> { return std::make_unique<McpContextFilter>(); },
    });
  }

  // Helper: create request headers with optional session ID.
  static Http::TestRequestHeaderMapImpl makeHeaders(absl::string_view session_id = "") {
    Http::TestRequestHeaderMapImpl hdrs{{":method", "POST"},
                                        {":path", "/mcp"},
                                        {"content-type", "application/json"}};
    if (!session_id.empty()) {
      hdrs.addCopy(Http::LowerCaseString{"mcp-session-id"}, std::string(session_id));
    }
    return hdrs;
  }

  std::unique_ptr<AiSessionManager> manager_;
  // Shared mock callbacks for direct newStream() calls in these tests.
  // Tests that care about sendLocalReply behavior use their own local mock.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
};

// Sessions are keyed by header: two requests with different IDs create two sessions.
TEST_F(AiSessionManagerTest, DifferentSessionIdsCreateDifferentSessions) {
  auto hdrs_a = makeHeaders("session-a");
  auto hdrs_b = makeHeaders("session-b");

  manager_->newStream(hdrs_a, callbacks_);
  manager_->newStream(hdrs_b, callbacks_);

  EXPECT_EQ(2u, manager_->sessionCount());
  EXPECT_NE(nullptr, manager_->findSession("session-a"));
  EXPECT_NE(nullptr, manager_->findSession("session-b"));
}

// Same session ID reuses the same AiSession across requests.
TEST_F(AiSessionManagerTest, SameSessionIdReusesSession) {
  auto hdrs = makeHeaders("persistent");

  manager_->newStream(hdrs, callbacks_); // request 1
  AiSession* first = manager_->findSession("persistent");
  ASSERT_NE(nullptr, first);

  manager_->newStream(hdrs, callbacks_); // request 2 — same session
  AiSession* second = manager_->findSession("persistent");
  EXPECT_EQ(first, second) << "Should reuse the same AiSession object";
  EXPECT_EQ(2u, first->requestCount());
}

// destroySession removes it; the next request recreates a fresh one.
TEST_F(AiSessionManagerTest, DestroyAndRecreateSession) {
  auto hdrs = makeHeaders("sess");
  manager_->newStream(hdrs, callbacks_);
  ASSERT_EQ(1u, manager_->sessionCount());

  // Manually authenticate and initialize the session.
  manager_->findSession("sess")->setIdentity("user");
  manager_->findSession("sess")->markInitialized({}, "2024-11-05");
  EXPECT_TRUE(manager_->findSession("sess")->isInitialized());

  EXPECT_TRUE(manager_->destroySession("sess"));
  EXPECT_EQ(0u, manager_->sessionCount());

  // New request — fresh session (not initialized).
  manager_->newStream(hdrs, callbacks_);
  EXPECT_EQ(1u, manager_->sessionCount());
  EXPECT_FALSE(manager_->findSession("sess")->isInitialized());
}

// No session header → anonymous session is created.
TEST_F(AiSessionManagerTest, MissingSessionHeaderUsesAnonymousSession) {
  auto hdrs = makeHeaders(); // no session ID
  manager_->newStream(hdrs, callbacks_);
  EXPECT_EQ(1u, manager_->sessionCount());
  EXPECT_NE(nullptr, manager_->findSession("__anonymous__"));
}

// Full e2e: manager.newStream() returns a JsonRpcDecoder; feed raw bytes.
// Session state (initialized, context) survives across the two requests.
TEST_F(AiSessionManagerTest, FullE2eInitializeThenToolCall) {
  auto hdrs = makeHeaders("e2e-session");

  // --- Request 1: initialize ---
  {
    JsonRpc::JsonRpcDecoder& decoder = manager_->newStream(hdrs, callbacks_);
    AiSession* session = manager_->findSession("e2e-session");
    ASSERT_NE(nullptr, session);
    session->setIdentity("user");

    ASSERT_TRUE(feedJson(decoder,
                         R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})").ok());
    EXPECT_TRUE(session->isInitialized());
  }

  // --- Request 2: tools/call ---
  {
    JsonRpc::JsonRpcDecoder& decoder = manager_->newStream(hdrs, callbacks_);
    ASSERT_TRUE(feedJson(decoder,
                         R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"search","arguments":{"q":"envoy"}}})").ok());

    AiSession* session = manager_->findSession("e2e-session");
    ASSERT_NE(nullptr, session);
    EXPECT_EQ(1u, session->context().size()); // McpContextFilter appended one turn
    EXPECT_EQ("tools/call", session->context()[0].at("method").get<std::string>());
  }
}

} // namespace
} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
