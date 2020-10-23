#include "envoy/config/tap/v3/common.pb.h"

#include "extensions/common/tap/admin.h"

#include "test/mocks/server/admin.h"
#include "test/mocks/server/admin_stream.h"

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

class MockExtensionConfig : public ExtensionConfig {
public:
  MOCK_METHOD(const absl::string_view, adminId, ());
  MOCK_METHOD(void, clearTapConfig, ());
  MOCK_METHOD(void, newTapConfig,
              (const envoy::config::tap::v3::TapConfig& proto_config, Sink* admin_streamer));
};

class AdminHandlerTest : public testing::Test {
public:
  AdminHandlerTest() {
    EXPECT_CALL(admin_, addHandler("/tap", "tap filter control", _, true, true))
        .WillOnce(DoAll(SaveArg<2>(&cb_), Return(true)));
    handler_ = std::make_unique<AdminHandler>(admin_, main_thread_dispatcher_);
  }

  ~AdminHandlerTest() override {
    EXPECT_CALL(admin_, removeHandler("/tap")).WillOnce(Return(true));
  }

  Server::MockAdmin admin_;
  Event::MockDispatcher main_thread_dispatcher_{"test_main_thread"};
  std::unique_ptr<AdminHandler> handler_;
  Server::Admin::HandlerCb cb_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Buffer::OwnedImpl response_;
  Server::MockAdminStream admin_stream_;

  const std::string admin_request_yaml_ =
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

// Request with no config body.
TEST_F(AdminHandlerTest, NoBody) {
  EXPECT_CALL(admin_stream_, getRequestBody());
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("/tap requires a JSON/YAML body", response_.toString());
}

// Request with a config body that doesn't parse/verify.
TEST_F(AdminHandlerTest, BadBody) {
  Buffer::OwnedImpl bad_body("hello");
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&bad_body));
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("Unable to convert YAML as JSON: hello", response_.toString());
}

// Request that references an unknown config ID.
TEST_F(AdminHandlerTest, UnknownConfigId) {
  Buffer::OwnedImpl body(admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("Unknown config id 'test_config_id'. No extension has registered with this id.",
            response_.toString());
}

// Request while there is already an active tap session.
TEST_F(AdminHandlerTest, RequestTapWhileAttached) {
  MockExtensionConfig extension_config;
  handler_->registerConfig(extension_config, "test_config_id");

  Buffer::OwnedImpl body(admin_request_yaml_);
  EXPECT_CALL(admin_stream_, getRequestBody()).WillRepeatedly(Return(&body));
  EXPECT_CALL(extension_config, newTapConfig(_, handler_.get()));
  EXPECT_CALL(admin_stream_, setEndStreamOnComplete(false));
  EXPECT_CALL(admin_stream_, addOnDestroyCallback(_));
  EXPECT_EQ(Http::Code::OK, cb_("/tap", response_headers_, response_, admin_stream_));

  EXPECT_EQ(Http::Code::BadRequest, cb_("/tap", response_headers_, response_, admin_stream_));
  EXPECT_EQ("An attached /tap admin stream already exists. Detach it.", response_.toString());
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
