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

  struct ResponseData {
    uint64_t num_chunks_{0};
    uint64_t num_bytes_{0};
    Http::Code code_;
    std::string content_type_;
  };

  ResponseData runStreamingRequest(AdminResponseSharedPtr response,
                                   std::function<void()> chunk_hook = nullptr) {
    absl::Notification done;
    std::vector<std::string> out;
    absl::Notification headers_notify;
    ResponseData response_data;
    response->getHeaders(
        [&headers_notify, &response_data](Http::Code code, Http::ResponseHeaderMap& headers) {
          response_data.code_ = code;
          response_data.content_type_ = headers.getContentTypeValue();
          headers_notify.Notify();
        });
    headers_notify.WaitForNotification();
    bool cont = true;
    while (cont && !response->cancelled()) {
      absl::Notification chunk_notify;
      response->nextChunk(
          [&chunk_notify, &response_data, &cont](Buffer::Instance& chunk, bool more) {
            cont = more;
            response_data.num_bytes_ += chunk.length();
            chunk.drain(chunk.length());
            ++response_data.num_chunks_;
            chunk_notify.Notify();
          });
      chunk_notify.WaitForNotification();
      if (chunk_hook != nullptr) {
        chunk_hook();
      }
    }

    return response_data;
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
   * The provided lambda runs in the main thread, between two notifications
   * controlled by this function.
   *
   * @param fn function to run in the main thread, before interlockMainThread returns.
   */
  void interlockMainThread(std::function<void()> fn) {
    main_common_->dispatcherForTest().post([this, fn] {
      resume_.WaitForNotification();
      fn();
      pause_point_.Notify();
    });
    resume_.Notify();
    pause_point_.WaitForNotification();
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
  ResponseData response_data = runStreamingRequest(response);
  EXPECT_EQ(StreamingAdminRequest::NumChunks, response_data.num_chunks_);
  EXPECT_EQ(StreamingAdminRequest::NumChunks * StreamingAdminRequest::BytesPerChunk,
            response_data.num_bytes_);
  EXPECT_EQ(Http::Code::OK, response_data.code_);
  EXPECT_EQ("text/plain; charset=UTF-8", response_data.content_type_);
  EXPECT_TRUE(quitAndWait());
}

TEST_F(AdminStreamingTest, QuitDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  AdminResponseSharedPtr response = streamingResponse();
  ResponseData response_data = runStreamingRequest(response, [&quit_counter, this]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      EXPECT_TRUE(quitAndWait());
    }
  });
  EXPECT_EQ(4, response_data.num_chunks_);
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            response_data.num_bytes_);
  EXPECT_EQ(Http::Code::OK, response_data.code_);
  EXPECT_EQ("text/plain; charset=UTF-8", response_data.content_type_);
}

TEST_F(AdminStreamingTest, CancelDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  AdminResponseSharedPtr response = streamingResponse();
  ResponseData response_data = runStreamingRequest(response, [response, &quit_counter]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      response->cancel();
    }
  });
  EXPECT_EQ(3, response_data.num_chunks_); // no final call to the chunk handler after cancel.
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            response_data.num_bytes_);
  EXPECT_EQ(Http::Code::OK, response_data.code_);
  EXPECT_EQ("text/plain; charset=UTF-8", response_data.content_type_);
  EXPECT_TRUE(quitAndWait());
}

TEST_F(AdminStreamingTest, CancelBeforeAskingForHeader) {
  AdminResponseSharedPtr response = streamingResponse();
  interlockMainThread([response]() { response->cancel(); });
  int header_calls = 0;

  // After 'cancel', the headers function will not be called.
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, header_calls);
}

TEST_F(AdminStreamingTest, CancelAfterAskingForHeader1) {
  int header_calls = 0;
  AdminResponseSharedPtr response = streamingResponse();
  interlockMainThread([&header_calls, response]() {
    response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
    response->cancel();
  });
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
  AdminResponseSharedPtr response = streamingResponse();
  interlockMainThread([&response, &header_calls]() {
    response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
    response.reset();
  });
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
  int chunk_calls = 0;
  interlockMainThread([&response, &chunk_calls]() {
    response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
    response->cancel();
  });
  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, chunk_calls);
}

TEST_F(AdminStreamingTest, CancelAfterAskingForChunk) {
  AdminResponseSharedPtr response = streamingResponse();
  waitForHeaders(response);
  int chunk_calls = 0;

  // Cause the /streaming handler to pause while yielding the next chunk, to hit
  // an early exit in requestNextChunk.
  next_chunk_hook_ = [response]() { response->cancel(); };

  interlockMainThread([&chunk_calls, response]() {
    response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
  });

  EXPECT_TRUE(quitAndWait());
  EXPECT_EQ(0, chunk_calls);
}

TEST_F(AdminStreamingTest, QuitBeforeHeaders) {
  AdminResponseSharedPtr response = streamingResponse();
  EXPECT_TRUE(quitAndWait());
  ResponseData response_data = runStreamingRequest(response);
  EXPECT_EQ(1, response_data.num_chunks_);
  EXPECT_EQ(0, response_data.num_bytes_);
  EXPECT_EQ(Http::Code::InternalServerError, response_data.code_);
  EXPECT_EQ("text/plain; charset=UTF-8", response_data.content_type_);
}

TEST_F(AdminStreamingTest, QuitDeleteRace1) {
  AdminResponseSharedPtr response = streamingResponse();
  // Initiates a streaming quit on the main thread, but do not wait for it.
  quitAndRequestHeaders();
  response.reset(); // Races with the quitquitquit
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_F(AdminStreamingTest, QuitDeleteRace2) {
  AdminResponseSharedPtr response = streamingResponse();
  adminRequest("/quitquitquit", "POST");
  response.reset();
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
  ResponseData response_data = runStreamingRequest(response);
  EXPECT_EQ(1, response_data.num_chunks_);
  EXPECT_EQ(0, response_data.num_bytes_);
  EXPECT_EQ(Http::Code::InternalServerError, response_data.code_);
  EXPECT_EQ("text/plain; charset=UTF-8", response_data.content_type_);
  resume_.Notify();
  EXPECT_TRUE(waitForEnvoyToExit());
  response.reset();
}

TEST_F(AdminStreamingTest, TimeoutGettingResponse) {
  absl::Notification got_headers;
  AdminResponseSharedPtr response = streamingResponse();

  // Mimics a slow admin response by adding a blocking notification in front
  // of a call to initiate an admin request.
  main_common_->dispatcherForTest().post([this, response, &got_headers] {
    resume_.WaitForNotification();
    response->getHeaders(
        [&got_headers](Http::Code, Http::ResponseHeaderMap&) { got_headers.Notify(); });
    pause_point_.Notify();
  });

  ENVOY_LOG_MISC(info, "Blocking for 5 seconds to test timeout functionality...");
  ASSERT_FALSE(got_headers.WaitForNotificationWithTimeout(absl::Seconds(5)));
  resume_.Notify();
  pause_point_.WaitForNotification();
  EXPECT_TRUE(quitAndWait());
}

} // namespace Envoy
