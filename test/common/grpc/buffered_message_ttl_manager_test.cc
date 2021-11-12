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

using testing::NiceMock;

class BufferedMessageTtlManagerTest : public testing::Test {
public:
  BufferedMessageTtlManagerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override {
    ttl_manager_ = std::make_shared<BufferedMessageTtlManager>(*dispatcher_, callbacks_,
                                                               std::chrono::milliseconds(1000));
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<BufferedMessageTtlManager> ttl_manager_;
  NiceMock<MockBufferedAsyncClientCallbacks> callbacks_;
};

TEST_F(BufferedMessageTtlManagerTest, Basic) {
  absl::flat_hash_set<uint64_t> ids{0, 1, 2};
  EXPECT_CALL(callbacks_, onError(_)).Times(3);
  ttl_manager_->setDeadline(std::move(ids));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
