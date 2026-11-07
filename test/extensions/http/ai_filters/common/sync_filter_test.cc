#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/http/ai_filters/common/sync_filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Common {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LocalReplier;

class SyncAiFilterTest : public testing::Test {
public:
  SyncAiFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~SyncAiFilterTest() override { buffer_manager_.onDestroy(); }

  void drain() {
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge bridge_;
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
};

class TestSyncMutationFilter : public SyncAiFilter {
public:
  explicit TestSyncMutationFilter(std::string model) : model_(std::move(model)) {}

  absl::Status decodeSync(AiRequest& request, LocalReplier) override {
    request.json()["model"] = model_;
    return absl::OkStatus();
  }

private:
  std::string model_;
};

TEST_F(SyncAiFilterTest, ContinueAndMutate) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-3.5"}});

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestSyncMutationFilter>("gpt-4o"));

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
                       [&status, &completed](absl::Status s) {
                         status = std::move(s);
                         completed = true;
                       });

  drain();
  EXPECT_TRUE(completed);
  ASSERT_OK(status);

  auto parsed = nlohmann::json::parse(bridge_.injected_.toString());
  EXPECT_EQ(parsed["model"], "gpt-4o");
}

class TestSyncLocalReplyFilter : public SyncAiFilter {
public:
  absl::Status decodeSync(AiRequest&, LocalReplier reply_locally) override {
    std::move(reply_locally)(Http::Code::Forbidden, "blocked by sync filter");
    return absl::OkStatus();
  }
};

TEST_F(SyncAiFilterTest, LocalReply) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::Code local_reply_code = Http::Code::OK;
  std::string local_reply_details;

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestSyncLocalReplyFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string details) {
        local_reply_code = code;
        local_reply_details = std::move(details);
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_EQ(local_reply_code, Http::Code::Forbidden);
  EXPECT_EQ(local_reply_details, "blocked by sync filter");
}

class TestSyncErrorFilter : public SyncAiFilter {
public:
  absl::Status decodeSync(AiRequest&, LocalReplier) override {
    return absl::InvalidArgumentError("bad request payload");
  }
};

TEST_F(SyncAiFilterTest, ErrorStatusTriggersLocalReply) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{{"model", "gpt-4"}});

  Http::Code local_reply_code = Http::Code::OK;
  std::string local_reply_details;

  std::vector<AiFilterSharedPtr> filters;
  filters.push_back(std::make_unique<TestSyncErrorFilter>());

  FilterManager manager(std::move(filters));

  absl::Status status;
  bool completed = false;
  manager.startRequest(
      std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
      [&status, &completed](absl::Status s) {
        status = std::move(s);
        completed = true;
      },
      /*request_headers=*/nullptr,
      [&local_reply_code, &local_reply_details](Http::Code code, std::string details) {
        local_reply_code = code;
        local_reply_details = std::move(details);
      });

  drain();
  EXPECT_TRUE(completed);
  EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(local_reply_code, Http::Code::BadGateway);
  EXPECT_EQ(local_reply_details, "bad request payload");
}

} // namespace
} // namespace Common
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
