#include "test/exe/main_common_test_base.h"

#include "gtest/gtest.h"

namespace Envoy {

class AdminStreamingTest : public AdminRequestTestBase, public testing::Test {
protected:
  static constexpr absl::string_view StreamingEndpoint = "/stream";

  class StreamingAdminRequest : public Envoy::Server::Admin::Request {
  public:
    static constexpr uint64_t NumChunks = 10;
    static constexpr uint64_t BytesPerChunk = 10000;

    StreamingAdminRequest(std::function<void()>& get_headers_hook,
                          std::function<void()>& next_chunk_hook)
        : chunk_(BytesPerChunk, 'a'), get_headers_hook_(get_headers_hook),
          next_chunk_hook_(next_chunk_hook) {}
    Http::Code start(Http::ResponseHeaderMap&) override {
      get_headers_hook_();
      return Http::Code::OK;
    }
    bool nextChunk(Buffer::Instance& response) override {
      next_chunk_hook_();
      response.add(chunk_);
      return --chunks_remaining_ > 0;
    }

  private:
    const std::string chunk_;
    uint64_t chunks_remaining_{NumChunks};
    std::function<void()>& get_headers_hook_;
    std::function<void()>& next_chunk_hook_;
  };

  AdminStreamingTest() : AdminRequestTestBase(Network::Address::IpVersion::v4) {
    startEnvoy();
    started_.WaitForNotification();
    Server::Admin& admin = *main_common_->server()->admin();
    admin.addStreamingHandler(
        std::string(StreamingEndpoint), "streaming api",
        [this](Server::AdminStream&) -> Server::Admin::RequestPtr {
          return std::make_unique<StreamingAdminRequest>(get_headers_hook_, next_chunk_hook_);
        },
        true, false);
  }

  using ChunksBytes = std::pair<uint64_t, uint64_t>;
  ChunksBytes runStreamingRequest(AdminResponseSharedPtr response,
                                  std::function<void()> chunk_hook = nullptr) {
    absl::Notification done;
    std::vector<std::string> out;
    absl::Notification headers_notify;
    response->getHeaders(
        [&headers_notify](Http::Code, Http::ResponseHeaderMap&) { headers_notify.Notify(); });
    headers_notify.WaitForNotification();
    bool cont = true;
    uint64_t num_chunks = 0;
    uint64_t num_bytes = 0;
    while (cont && !response->cancelled()) {
      absl::Notification chunk_notify;
      response->nextChunk(
          [&chunk_notify, &num_chunks, &num_bytes, &cont](Buffer::Instance& chunk, bool more) {
            cont = more;
            num_bytes += chunk.length();
            chunk.drain(chunk.length());
            ++num_chunks;
            chunk_notify.Notify();
          });
      chunk_notify.WaitForNotification();
      if (chunk_hook != nullptr) {
        chunk_hook();
      }
    }

    return ChunksBytes(num_chunks, num_bytes);
  }

  /**
   * @return a streaming response to a GET of StreamingEndpoint.
   */
  AdminResponseSharedPtr streamingResponse() {
    return main_common_->adminRequest(StreamingEndpoint, "GET");
  }

  /**
   * In order to trigger certain early-exit criteria in a test, we can exploit
   * the fact that all the admin responses are delivered on the main thread.
   * So we can pause those by blocking the main thread indefinitely.
   *
   * To resume the main thread, call resume_.Notify();
   *
   * @param url the stats endpoint to initiate.
   */
  void blockMainThreadUntilResume(absl::string_view url, absl::string_view method) {
    AdminResponseSharedPtr blocked_response = main_common_->adminRequest(url, method);
    absl::Notification block_main_thread;
    blocked_response->getHeaders(
        [this](Http::Code, Http::ResponseHeaderMap&) { resume_.WaitForNotification(); });
  }

  /**
   * To hit an early exit after the second lock in
   * AdminResponseImpl::requestNextChunk and AdminResponseImpl::requestHeaders
   * we, need to post a cancel request to the main thread, but block that on the
   * resume_ notification. This allows a subsequent test call to getHeaders or
   * nextChunk initiate a post to main thread, but runs the cancel call on the
   * response before headers_fn_ or body_fn_ runs.
   *
   * @param response the response to cancel after resume_.
   */
  void blockMainThreadAndCancelResponseAfterResume(AdminResponseSharedPtr response) {
    main_common_->dispatcherForTest().post([response, this] {
      resume_.WaitForNotification();
      response->cancel();
    });
  }

  /**
   * Requests the headers and waits until the headers have been sent.
   *
   * @param response the response from which to get headers.
   */
  void waitForHeaders(AdminResponseSharedPtr response) {
    absl::Notification headers_notify;
    response->getHeaders(
        [&headers_notify](Http::Code, Http::ResponseHeaderMap&) { headers_notify.Notify(); });
    headers_notify.WaitForNotification();
  }

  /**
   * Initiates a '/quitquitquit' call and requests the headers for that call,
   * but does not wait for the call to complete. We avoid waiting in order to
   * trigger a potential race to ensure that MainCommon handles it properly.
   */
  void quitAndRequestHeaders() {
    AdminResponseSharedPtr quit_response = main_common_->adminRequest("/quitquitquit", "POST");
    quit_response->getHeaders([](Http::Code, Http::ResponseHeaderMap&) {});
  }

  // This variable provides a hook to allow a test method to specify a hook to
  // run when nextChunk() is called. This is currently used only for one test,
  // CancelAfterAskingForChunk, that initiates a cancel() from within the chunk
  // handler.
  std::function<void()> get_headers_hook_ = []() {};
  std::function<void()> next_chunk_hook_ = []() {};
};

TEST_F(AdminStreamingTest, RequestGetStatsAndQuit) {
  AdminResponseSharedPtr response = streamingResponse();
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(StreamingAdminRequest::NumChunks, chunks_bytes.first);
  EXPECT_EQ(StreamingAdminRequest::NumChunks * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
  EXPECT_TRUE(quitAndWait());
}

TEST_F(AdminStreamingTest, QuitDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  AdminResponseSharedPtr response = streamingResponse();
  ChunksBytes chunks_bytes = runStreamingRequest(response, [&quit_counter, this]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      EXPECT_TRUE(quitAndWait());
    }
  });
  EXPECT_EQ(4, chunks_bytes.first);
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
}

TEST_F(AdminStreamingTest, CancelDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  AdminResponseSharedPtr response = streamingResponse();
  ChunksBytes chunks_bytes = runStreamingRequest(response, [response, &quit_counter]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      response->cancel();
    }
  });
  EXPECT_EQ(3, chunks_bytes.first); // no final call to the chunk handler after cancel.
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
  EXPECT_TRUE(quitAndWait());
}

TEST_F(AdminStreamingTest, CancelBeforeAskingForHeader1) {
  blockMainThreadUntilResume("/ready", "GET");
  AdminResponseSharedPtr response = streamingResponse();
  response->cancel();
  resume_.Notify();
  int header_calls = 0;

  // After 'cancel', the headers function will not be called.
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, header_calls);
}

TEST_F(AdminStreamingTest, CancelBeforeAskingForHeader2) {
  AdminResponseSharedPtr response = streamingResponse();
  blockMainThreadAndCancelResponseAfterResume(response);
  int header_calls = 0;
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  resume_.Notify();
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, header_calls);
}

TEST_F(AdminStreamingTest, CancelAfterAskingForHeader1) {
  int header_calls = 0;
  blockMainThreadUntilResume("/ready", "GET");
  AdminResponseSharedPtr response = streamingResponse();
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  response->cancel();
  resume_.Notify();
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, header_calls);
}

TEST_F(AdminStreamingTest, CancelAfterAskingForHeader2) {
  int header_calls = 0;
  AdminResponseSharedPtr response = streamingResponse();
  get_headers_hook_ = [&response]() { response->cancel(); };
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, header_calls);
}

TEST_F(AdminStreamingTest, DeleteAfterAskingForHeader1) {
  int header_calls = 0;
  blockMainThreadUntilResume("/ready", "GET");
  AdminResponseSharedPtr response = streamingResponse();
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  response.reset();
  resume_.Notify();
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(1, header_calls);
}

TEST_F(AdminStreamingTest, DeleteAfterAskingForHeader2) {
  int header_calls = 0;
  AdminResponseSharedPtr response = streamingResponse();
  get_headers_hook_ = [&response]() { response.reset(); };
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(1, header_calls);
}

TEST_F(AdminStreamingTest, CancelBeforeAskingForChunk1) {
  AdminResponseSharedPtr response = streamingResponse();
  waitForHeaders(response);
  response->cancel();
  int chunk_calls = 0;
  response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, chunk_calls);
}

TEST_F(AdminStreamingTest, CancelBeforeAskingForChunk2) {
  AdminResponseSharedPtr response = streamingResponse();
  waitForHeaders(response);
  blockMainThreadAndCancelResponseAfterResume(response);
  int chunk_calls = 0;
  response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
  resume_.Notify();
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, chunk_calls);
}

TEST_F(AdminStreamingTest, CancelAfterAskingForChunk) {
  AdminResponseSharedPtr response = streamingResponse();
  waitForHeaders(response);
  blockMainThreadUntilResume("/ready", "GET");
  int chunk_calls = 0;

  // Cause the /streaming handler to pause while yielding the next chunk, to hit
  // an early exit in MainCommonBase::requestNextChunk.
  next_chunk_hook_ = [response]() { response->cancel(); };
  response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
  resume_.Notify();
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, chunk_calls);
}

TEST_F(AdminStreamingTest, QuitBeforeHeaders) {
  AdminResponseSharedPtr response = streamingResponse();
  EXPECT_TRUE(quitAndWait());
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(1, chunks_bytes.first);
  EXPECT_EQ(0, chunks_bytes.second);
}

TEST_F(AdminStreamingTest, QuitDeleteRace) {
  AdminResponseSharedPtr response = streamingResponse();
  // Initiates a streaming quit on the main thread, but do not wait for it.
  quitAndRequestHeaders();
  response.reset(); // Races with the quitquitquit
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_F(AdminStreamingTest, QuitCancelRace) {
  AdminResponseSharedPtr response = streamingResponse();
  quitAndRequestHeaders();
  response->cancel(); // Races with the quitquitquit
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_F(AdminStreamingTest, QuitBeforeCreatingResponse) {
  // Initiates a streaming quit on the main thread, and wait for headers, which
  // will trigger the termination of the event loop, and subsequent nulling of
  // main_common_. However we can pause the test infrastructure after the quit
  // takes hold leaving main_common_ in tact, to reproduce a potential race.
  pause_after_run_ = true;
  adminRequest("/quitquitquit", "POST");
  pause_point_.WaitForNotification(); // run() finished, but main_common_ still exists.
  AdminResponseSharedPtr response = streamingResponse();
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(1, chunks_bytes.first);
  EXPECT_EQ(0, chunks_bytes.second);
  resume_.Notify();
  EXPECT_TRUE(waitForEnvoyToExit());
  response.reset();
}

TEST_F(AdminStreamingTest, QuitTerminateRace) {
  AdminResponseSharedPtr response = streamingResponse();
  adminRequest("/quitquitquit", "POST");
  response.reset();
  EXPECT_TRUE(waitForEnvoyToExit());
}

} // namespace Envoy
