#include <chrono>
#include <cstdint>
#include <memory>

#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/buffered_message_ttl_manager.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

class BufferedMessageTtlManagerTest : public testing::Test {
public:
  BufferedMessageTtlManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<BufferedMessageTtlManager> ttl_manager_;
  std::chrono::milliseconds msec_{1000};
  uint32_t callback_called_counter_ = 0;
};

// In this test, we will test the basic TTL Manager behavior,
// making sure that the buffers for identity management are in the proper state after deadline.
TEST_F(BufferedMessageTtlManagerTest, BasicFlow) {
  absl::flat_hash_set<uint64_t> ids{0};
  ttl_manager_ = std::make_shared<BufferedMessageTtlManager>(
      *dispatcher_,
      [this](uint64_t) {
        switch (callback_called_counter_) {
        case 0: {
          EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 1);
          absl::flat_hash_set<uint64_t> ids{1, 2};
          ttl_manager_->addDeadlineEntry(std::move(ids));
        } break;
        case 1:
        case 2:
        case 3:
          EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 1);
          break;
        default:
          break;
        }
        ++callback_called_counter_;
      },
      msec_);
  ttl_manager_->addDeadlineEntry(std::move(ids));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Test if deadline queue is empty after queue cleared once.
  EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 0);

  absl::flat_hash_set<uint64_t> ids2{3};
  ttl_manager_->addDeadlineEntry(std::move(ids2));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(callback_called_counter_, 4);
  EXPECT_EQ(ttl_manager_->deadlineForTest().size(), 0);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
