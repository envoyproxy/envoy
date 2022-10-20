#include <chrono>
#include <memory>

#include "envoy/config/tap/v3/common.pb.h"

#include "source/extensions/common/tap/admin.h"
#include "source/extensions/common/tap/tap.h"

#include "test/mocks/server/admin.h"
#include "test/mocks/server/admin_stream.h"
#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Between;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::StrictMock;

class MockExtensionConfig : public ExtensionConfig {
public:
  MOCK_METHOD(const absl::string_view, adminId, ());
  MOCK_METHOD(void, clearTapConfig, ());
  MOCK_METHOD(void, newTapConfig,
              (const envoy::config::tap::v3::TapConfig& proto_config, Sink* admin_streamer));
};

// MockDispatcherQueued buffers posted callbacks until they are explicitly asked
// to execute by calling either step or drain.
// Buffering is performed to allow sequences of posts that will cause issues if the posted callbacks
// execute immediately. For example in BufferedTapTimeoutRace, posting the callback destroying the
// attached request followed by triggering the timeout. As triggering the timeout dereferences the
// attached request, the callbacks cannot be executed immediately when they are posted (as is
// default) as this would trigger a segmentation fault.
class MockDispatcherQueued : public Event::MockDispatcher {
public:
  MockDispatcherQueued(const std::string& name) : Event::MockDispatcher(name) {
    ON_CALL(*this, post).WillByDefault([this](std::function<void()> callback) {
      callbacks_.push(callback);
    });
  }

  bool step() {
    if (callbacks_.empty()) {
      return false;
    }
    callbacks_.front()();
    callbacks_.pop();
    return true;
  }

  void drain() {
    while (step()) {
    }
  }

private:
  std::queue<std::function<void()>> callbacks_;
};

class BaseAdminHandlerTest : public testing::Test {
public:
  void setup(Network::Address::Type socket_type = Network::Address::Type::Ip) {
    ON_CALL(admin_.socket_, addressType()).WillByDefault(Return(socket_type));
    EXPECT_CALL(admin_, addHandler("/tap", "tap filter control", _, true, true, _))
        .WillOnce(DoAll(SaveArg<2>(&cb_), Return(true)));
    EXPECT_CALL(admin_, socket());
    handler_ = std::make_unique<AdminHandler>(admin_, main_thread_dispatcher_);
  }

  void TearDown() override { main_thread_dispatcher_.drain(); }

  ~BaseAdminHandlerTest() override {
    EXPECT_CALL(admin_, removeHandler("/tap")).WillOnce(Return(true));
  }

  std::shared_ptr<AdminHandler::AttachedRequest>& attachedRequest() {
    return handler_->attached_request_;
  }

  Http::Code makeRequest(absl::string_view path) {
    Http::TestResponseHeaderMapImpl response_headers;
    request_headers_.setPath(path);
    EXPECT_CALL(admin_stream_, getRequestHeaders()).WillRepeatedly(ReturnRef(request_headers_));
    return cb_(response_headers, response_, admin_stream_);
  }

protected:
  Server::MockAdmin admin_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Server::StrictMockAdminStream admin_stream_;
  std::unique_ptr<AdminHandler> handler_;
  Server::Admin::HandlerCb cb_;
  MockDispatcherQueued main_thread_dispatcher_{"test_main_thread"};
  Buffer::OwnedImpl response_;
};

class AdminHandlerTest : public BaseAdminHandlerTest {
public:
  const std::string streaming_admin_request_yaml_ =
      R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - streaming_admin: {}
)EOF";
};

class BufferedAdminHandlerTest : public BaseAdminHandlerTest {
public:
  AdminHandler::AttachedRequestBuffered* attachedRequestBuffered() {
    return dynamic_cast<AdminHandler::AttachedRequestBuffered*>(attachedRequest().get());
  }

  void triggerTimeout() { attachedRequestBuffered()->onTimeout(attachedRequest()); }

  std::string makeBufferedAdminYaml(uint64_t max_traces, std::string timeout_s = "0s") {
    const std::string buffered_admin_request_yaml_ =
        R"EOF(
config_id: test_config_id
tap_config:
  match:
    any_match: true
  output_config:
    sinks:
      - buffered_admin:
          max_traces: {}
          timeout: {}
)EOF";
    return fmt::format(buffered_admin_request_yaml_, max_traces, timeout_s);
  }

  // Cannot be moved into individual test cases as expected calls are validated on object
  // destruction, and the code that satisfies the expected calls on sink_ is in the TearDown method.
  StrictMock<Http::MockStreamDecoderFilterCallbacks> sink_;
};

// Make sure warn if using a pipe address for the admin handler.
TEST_F(AdminHandlerTest, AdminWithPipeSocket) {
  EXPECT_LOG_CONTAINS(
      "warn",
      "Admin tapping (via /tap) is unreliable when the admin endpoint is a pipe and the connection "
      "is HTTP/1. Either use an IP address or connect using HTTP/2.",
      setup(Network::Address::Type::Pipe));
}

// Request with no config body.
TEST_F(AdminHandlerTest, NoBody) {
  setup();
  EXPECT_CALL(admin_stream_, getRequestBody());
  EXPECT_EQ(Http::Code::BadRequest, makeRequest("/tap"));
  EXPECT_EQ("/tap requires a JSON/YAML body", response_.toString());
}

// Request with a config body that doesn't parse/verify.
TEST_F(AdminHandlerTest, BadBody) {
  setup();
  Buffer::OwnedImpl bad_body("hello");
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&bad_body));
  EXPECT_EQ(Http::Code::BadRequest, makeRequest("/tap"));
  EXPECT_EQ("Unable to convert YAML as JSON: hello", response_.toString());
}

// Request that references an unknown config ID.
TEST_F(AdminHandlerTest, UnknownConfigId) {
  setup();
  Buffer::OwnedImpl body(streaming_admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_EQ(Http::Code::BadRequest, makeRequest("/tap"));
  EXPECT_EQ("Unknown config id 'test_config_id'. No extension has registered with this id.",
            response_.toString());
}

// Request while there is already an active tap session.
TEST_F(AdminHandlerTest, RequestTapWhileAttached) {
  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");

  Buffer::OwnedImpl body(streaming_admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  EXPECT_EQ(Http::Code::BadRequest, makeRequest("/tap"));
  EXPECT_EQ("An attached /tap admin stream already exists. Detach it.", response_.toString());
}

TEST_F(AdminHandlerTest, CloseMidStream) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");

  Buffer::OwnedImpl body(streaming_admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sinkHandle =
      handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kStreamingAdmin);

  EXPECT_CALL(main_thread_dispatcher_, post(_)).Times(2);
  main_thread_dispatcher_.post([this] { attachedRequest().reset(); });
  sinkHandle->submitTrace(makeTraceWrapper(), format);
}

// Test that when performing a buffered tap, verify the admin_stream_ encodes each packet.
TEST_F(BufferedAdminHandlerTest, BufferedTapWrites) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;

  const int traces = 4;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");
  Buffer::OwnedImpl body(makeBufferedAdminYaml(traces));
  ON_CALL(admin_stream_, getDecoderFilterCallbacks()).WillByDefault(ReturnRef(sink_));

  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_CALL(admin_stream_, getDecoderFilterCallbacks()).Times(AtLeast(traces));
  EXPECT_CALL(sink_, encodeData(_, _)).Times(Between(traces, traces + 1));
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sinkHandle =
      handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin);
  // Send more traces than are needed to fill the buffer - extra traces should be discarded
  for (int i = 0; i < traces * 2; i++) {
    EXPECT_CALL(main_thread_dispatcher_, post(_));
    sinkHandle->submitTrace(makeTraceWrapper(), format);
  }
}

// BufferedTapRace tests functionality in a race case in the buffered admin sink. In particular,
// this test puts the main thread dispatcher queue into a state where a submitTrace callback
// follows an on destroy callback that destroys the attached request. If handled improperly, this
// case trigger a null pointer exception accessing the attached request.
//
// This case would be executed via normal code flow in the following sequence:
// 1. Buffer becomes full and posts to the main thread dispatcher
// 2. Dispatcher runs this post and flushes the buffer calling encodeData with end stream set to
// true
// 3. The on destroy callback for the admin stream is posted to the dispatcher
// 4. Another trace is submitted and submitTrace posts to the dispatcher
// 5. The dispatcher executes the on destroy callback deleting the attached request
// 6. The dispatcher executes the post from step 4
TEST_F(BufferedAdminHandlerTest, BufferedTapRace) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");
  Buffer::OwnedImpl body(makeBufferedAdminYaml(1));

  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_CALL(main_thread_dispatcher_, post(_)).Times(2);
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sink_handle =
      handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin);

  main_thread_dispatcher_.post([this] { attachedRequest().reset(); });
  sink_handle->submitTrace(makeTraceWrapper(), format);
}

// BufferedTapTimeoutRace tests functionality in a race case in the buffered admin sink.
// In particular this test puts the main thread dispatcher queue into a state where a
// callback posted due to a timeout follows an on destroy callback which destroys the attached
// request. If handled improperly, this case trigger a null pointer exception accessing the attached
// request.
//
// This behavior would be executed via normal code flow in the following sequence:
// 1. Buffer becomes full and submitTrace posts to the dispatcher
// 2. The timeout expires but before the on timeout callback is posted to the dispatcher, a context
// switch occurs
// 3. The callback posted in step 1 runs calling encode data with end stream true, posting the on
// destroy callback for the admin stream
// 4. The code from step 2 context switches back in, and posts the on timeout callback to the
// dispatcher
// 5. The on destroy callback from step 3 runs
// 6. The on timeout callback from step 4 runs
TEST_F(BufferedAdminHandlerTest, BufferedTapTimeoutRace) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");
  // Don't pass a timeout, callback will be called artificially
  Buffer::OwnedImpl body(makeBufferedAdminYaml(1));
  ON_CALL(admin_stream_, getDecoderFilterCallbacks()).WillByDefault(ReturnRef(sink_));

  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_CALL(main_thread_dispatcher_, post(_)).Times(3);
  EXPECT_CALL(admin_stream_, getDecoderFilterCallbacks()).Times(AtLeast(1));
  EXPECT_CALL(sink_, encodeData(_, _)).Times(Between(1, 2));
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sink_handle =
      handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin);

  sink_handle->submitTrace(makeTraceWrapper(), format);
  main_thread_dispatcher_.post([this] { attachedRequest().reset(); });
  triggerTimeout();
}

// BufferedTapDoubleFlush tests functionality in a race case in the buffered admin sink.
// In particular this test puts the main thread dispatcher queue into a state where a
// callback posted by submitTrace that will flush the buffer is followed by a callback
// posted due to a timeout. If not handled properly this could lead to attempting to
// buffer flush twice.
//
// This behavior would be executed via normal code flow in the following sequence:
// 1. Buffer becomes full and submitTrace posts to the dispatcher.
// 2. The timeout expires and the on timeout callback is posted to the dispatcher.
// 3. The callback posted in step 1 runs calling encode data with end stream true, posting the on
// destroy callback for the admin stream.
// 4. The timeout callback from step 2 runs.
// 5. The on destroy callback from step 3 runs.
TEST_F(BufferedAdminHandlerTest, BufferedTapDoubleFlush) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");
  // Don't pass a timeout, timeout callback will be called artificially
  Buffer::OwnedImpl body(makeBufferedAdminYaml(1));
  ON_CALL(admin_stream_, getDecoderFilterCallbacks()).WillByDefault(ReturnRef(sink_));

  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_CALL(admin_stream_, getDecoderFilterCallbacks()).Times(AtLeast(1));
  EXPECT_CALL(main_thread_dispatcher_, post(_)).Times(2);
  EXPECT_CALL(sink_, encodeData(_, _)).Times(Between(1, 2));
  EXPECT_EQ(Http::Code::OK, makeRequest("/tap"));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sink_handle =
      handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin);

  sink_handle->submitTrace(makeTraceWrapper(), format);
  triggerTimeout();
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
