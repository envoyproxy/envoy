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
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::AtLeast;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::Between;

class MockExtensionConfig : public ExtensionConfig {
public:
  MOCK_METHOD(const absl::string_view, adminId, ());
  MOCK_METHOD(void, clearTapConfig, ());
  MOCK_METHOD(void, newTapConfig,
              (const envoy::config::tap::v3::TapConfig& proto_config, Sink* admin_streamer));
};

class AdminHandlerTest : public testing::Test {
public:
  void setup(Network::Address::Type socket_type = Network::Address::Type::Ip) {
    ON_CALL(admin_.socket_, addressType()).WillByDefault(Return(socket_type));
    EXPECT_CALL(admin_, addHandler("/tap", "tap filter control", _, true, true))
        .WillOnce(DoAll(SaveArg<2>(&cb_), Return(true)));
    EXPECT_CALL(admin_, socket());
    handler_ = std::make_unique<AdminHandler>(admin_, main_thread_dispatcher_);
  }

  ~AdminHandlerTest() override {
    EXPECT_CALL(admin_, removeHandler("/tap")).WillOnce(Return(true));
  }

  std::string makeBufferedAdminYaml(uint64_t max_traces) {
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
)EOF";
    return fmt::format(buffered_admin_request_yaml_, max_traces);
  }

  Server::MockAdmin admin_;
  Event::MockDispatcher main_thread_dispatcher_{"test_main_thread"};
  std::unique_ptr<AdminHandler> handler_;
  Server::Admin::HandlerCb cb_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Buffer::OwnedImpl response_;
  Server::MockAdminStream admin_stream_;

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
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("/tap requires a JSON/YAML body", response_.toString());
}

// Request with a config body that doesn't parse/verify.
TEST_F(AdminHandlerTest, BadBody) {
  setup();
  Buffer::OwnedImpl bad_body("hello");
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&bad_body));
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("Unable to convert YAML as JSON: hello", response_.toString());
}

// Request that references an unknown config ID.
TEST_F(AdminHandlerTest, UnknownConfigId) {
  setup();
  Buffer::OwnedImpl body(streaming_admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
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
  EXPECT_EQ(Http::Code::OK, cb_("/tap", response_headers_, response_, admin_stream_));

  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("An attached /tap admin stream already exists. Detach it.", response_.toString());
}

// Test that when performing a buffered tap, verify the admin_stream_ encodes each packet.
TEST_F(AdminHandlerTest, BufferedTapWrites) {
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;

  const int traces = 4;
  const ProtoOutputSink::Format format = ProtoOutputSink::JSON_BODY_AS_BYTES;

  setup();
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");
  Buffer::OwnedImpl body(makeBufferedAdminYaml(traces));

  // Needed to enforce a count on the number of calls to encodeData
  Server::StrictMockAdminStream strict_admin_stream;

  EXPECT_CALL(strict_admin_stream, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(strict_admin_stream, setEndStreamOnComplete(false));
  EXPECT_CALL(strict_admin_stream, addOnDestroyCallback(_));
  EXPECT_EQ(Http::Code::OK, cb_("/tap", response_headers_, response_, strict_admin_stream));

  StrictMock<Http::MockStreamDecoderFilterCallbacks> mockDecoderCallbacks;
  ON_CALL(strict_admin_stream, getDecoderFilterCallbacks()).WillByDefault(ReturnRef(mockDecoderCallbacks));

  EXPECT_CALL(strict_admin_stream, getDecoderFilterCallbacks());
  StrictMock<Http::MockStreamDecoderFilterCallbacks>& sink = strict_admin_stream.getDecoderFilterCallbacks();

  // Called once for each buffered item, but can be called for other purposes, no need to be strict
  EXPECT_CALL(strict_admin_stream, getDecoderFilterCallbacks()).Times(AtLeast(traces));
  // One call per buffered trace with a possible final call to close the stream.
  EXPECT_CALL(sink, encodeData(_, _)).Times(Between(traces, traces + 1));

  // Direct access to the handle is required so we can submit traces directly
  PerTapSinkHandlePtr sinkHandle = handler_->createPerTapSinkHandle(0, ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin);
  // Send more traces than are needed to fill the buffer - extra traces should be discarded
  for (int i = 0; i < traces * 2; i++) {
    EXPECT_CALL(main_thread_dispatcher_, post(_));
    sinkHandle->submitTrace(makeTraceWrapper(), format);
  }
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
