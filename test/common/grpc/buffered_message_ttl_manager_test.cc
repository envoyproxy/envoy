#include <chrono>
#include <memory>

#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/buffered_message_ttl_manager.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

class MockBufferedAsyncClientCallbacks : public BufferedAsyncClientCallbacks {
public:
  MOCK_METHOD(void, onSuccess, (uint64_t), ());
  MOCK_METHOD(void, onError, (uint64_t), ());
};

using testing::Invoke;
using testing::NiceMock;

class BufferedMessageTtlManagerTest : public testing::Test {
public:
  BufferedMessageTtlManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override {
    ttl_manager_ = std::make_shared<BufferedMessageTtlManager>(*dispatcher_, callbacks_, msec_);
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<BufferedMessageTtlManager> ttl_manager_;
  NiceMock<MockBufferedAsyncClientCallbacks> callbacks_;
  std::chrono::milliseconds msec_{1000};
};

TEST_F(BufferedMessageTtlManagerTest, Basic) {
  absl::flat_hash_set<uint64_t> ids{0};
  EXPECT_CALL(callbacks_, onError(_)).WillOnce(Invoke([this] {
    EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 1);
    absl::flat_hash_set<uint64_t> ids{1, 2};
    EXPECT_CALL(callbacks_, onError(_)).Times(2);
    ttl_manager_->addDeadlineEntry(std::move(ids));
  }));
  ttl_manager_->addDeadlineEntry(std::move(ids));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 0);

  // Test if deadline queue is empty after queue cleared once.
  absl::flat_hash_set<uint64_t> ids2{3};
  EXPECT_CALL(callbacks_, onError(_)).WillOnce(Invoke([this] {
    EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 1);
  }));
  ttl_manager_->addDeadlineEntry(std::move(ids2));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 0);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
