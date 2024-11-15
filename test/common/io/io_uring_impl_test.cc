#include <functional>

#include "source/common/io/io_uring_impl.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/io/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

class TestRequest : public Request {
public:
  explicit TestRequest(int& data)
      : Request(RequestType::Read, mock_io_uring_socket_), data_(data) {}
  ~TestRequest() override { data_ = -1; }

  int& data_;
  MockIoUringSocket mock_io_uring_socket_;
};

using WaitConditionFunc = std::function<bool()>;

class IoUringImplTest : public ::testing::Test {
public:
  IoUringImplTest() : api_(Api::createApiForTest()), should_skip_(!isIoUringSupported()) {
    if (!should_skip_) {
      io_uring_ = std::make_unique<IoUringImpl>(2, false);
    }
  }

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  void TearDown() override {
    if (should_skip_) {
      return;
    }

    if (io_uring_->isEventfdRegistered()) {
      io_uring_->unregisterEventfd();
    }
  }

  void waitForCondition(Event::Dispatcher& dispatcher, WaitConditionFunc condition_func,
                        std::chrono::milliseconds wait_timeout = TestUtility::DefaultTimeout) {
    Event::TestTimeSystem::RealTimeBound bound(wait_timeout);
    while (!condition_func()) {
      if (!bound.withinBound()) {
        RELEASE_ASSERT(0, "Timed out waiting for the condition.");
        break;
      }
      dispatcher.run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  Api::ApiPtr api_;
  IoUringPtr io_uring_{};
  const bool should_skip_{};
};

class IoUringImplParamTest
    : public IoUringImplTest,
      public testing::WithParamInterface<std::function<IoUringResult(IoUring&, os_fd_t)>> {};

INSTANTIATE_TEST_SUITE_P(
    InvalidPrepareMethodParamsTest, IoUringImplParamTest,
    testing::Values(
        [](IoUring& uring, os_fd_t fd) -> IoUringResult {
          return uring.prepareAccept(fd, nullptr, nullptr, nullptr);
        },
        [](IoUring& uring, os_fd_t fd) -> IoUringResult {
          auto address = std::make_shared<Network::Address::EnvoyInternalInstance>("test");
          return uring.prepareConnect(fd, address, nullptr);
        },
        [](IoUring& uring, os_fd_t fd) -> IoUringResult {
          return uring.prepareReadv(fd, nullptr, 0, 0, nullptr);
        },
        [](IoUring& uring, os_fd_t fd) -> IoUringResult {
          return uring.prepareWritev(fd, nullptr, 0, 0, nullptr);
        },
        [](IoUring& uring, os_fd_t fd) -> IoUringResult { return uring.prepareClose(fd, nullptr); },
        [](IoUring& uring, os_fd_t fd) -> IoUringResult {
          return uring.prepareShutdown(fd, 0, nullptr);
        }));

TEST_P(IoUringImplParamTest, InvalidParams) {
  os_fd_t fd;
  SET_SOCKET_INVALID(fd);
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](Request*, int32_t res, bool) {
          EXPECT_TRUE(res < 0);
          completions_nr++;
        });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  auto prepare_method = GetParam();
  IoUringResult res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(*io_uring_, fd);
  EXPECT_EQ(res, IoUringResult::Failed);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 2; });
}

TEST_F(IoUringImplTest, InjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  int data1 = 1;
  TestRequest request(data1);

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [&completions_nr](Request* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              EXPECT_EQ(1, dynamic_cast<TestRequest*>(user_data)->data_);
              EXPECT_EQ(-11, res);
              completions_nr++;
            });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &request, -11);

  file_event->activate(Event::FileReadyType::Read);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 1; });
}

TEST_F(IoUringImplTest, NestInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 11;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  int data1 = 1;
  TestRequest request(data1);
  int data2 = 2;
  TestRequest request2(data2);

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd2, &completions_nr, &request2](uint32_t) {
        io_uring_->forEveryCompletion([this, &fd2, &completions_nr,
                                       &request2](Request* user_data, int32_t res, bool injected) {
          EXPECT_TRUE(injected);
          if (completions_nr == 0) {
            EXPECT_EQ(1, dynamic_cast<TestRequest*>(user_data)->data_);
            EXPECT_EQ(-11, res);
            io_uring_->injectCompletion(fd2, &request2, -22);
          } else {
            EXPECT_EQ(2, dynamic_cast<TestRequest*>(user_data)->data_);
            EXPECT_EQ(-22, res);
          }

          completions_nr++;
        });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &request, -11);

  file_event->activate(Event::FileReadyType::Read);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 2; });
}

TEST_F(IoUringImplTest, RemoveInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 22;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  int data1 = 1;
  TestRequest request(data1);
  int data2 = 2;
  TestRequest* request2 = new TestRequest(data2);

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion(
            [&completions_nr](Request* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              EXPECT_EQ(1, dynamic_cast<TestRequest*>(user_data)->data_);
              EXPECT_EQ(-11, res);
              completions_nr++;
            });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &request, -11);
  io_uring_->injectCompletion(fd2, request2, -22);
  io_uring_->removeInjectedCompletion(fd2);
  EXPECT_EQ(-1, data2);
  file_event->activate(Event::FileReadyType::Read);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 1; });
}

TEST_F(IoUringImplTest, NestRemoveInjectCompletion) {
  auto dispatcher = api_->allocateDispatcher("test_thread");

  os_fd_t fd = 11;
  os_fd_t fd2 = 22;
  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  int data1 = 1;
  TestRequest request(data1);
  int data2 = 2;
  TestRequest* request2 = new TestRequest(data2);

  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &fd2, &completions_nr, &data2](uint32_t) {
        io_uring_->forEveryCompletion(
            [this, &fd2, &completions_nr, &data2](Request* user_data, int32_t res, bool injected) {
              EXPECT_TRUE(injected);
              if (completions_nr == 0) {
                EXPECT_EQ(1, dynamic_cast<TestRequest*>(user_data)->data_);
                EXPECT_EQ(-11, res);
              } else {
                io_uring_->removeInjectedCompletion(fd2);
                EXPECT_EQ(-1, data2);
              }
              completions_nr++;
            });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->injectCompletion(fd, &request, -11);
  io_uring_->injectCompletion(fd2, request2, -22);

  file_event->activate(Event::FileReadyType::Read);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 2; });
}

TEST_F(IoUringImplTest, RegisterEventfd) {
  EXPECT_FALSE(io_uring_->isEventfdRegistered());
  io_uring_->registerEventfd();
  EXPECT_TRUE(io_uring_->isEventfdRegistered());
  io_uring_->unregisterEventfd();
  EXPECT_FALSE(io_uring_->isEventfdRegistered());
  EXPECT_DEATH(io_uring_->unregisterEventfd(), "");
}

TEST_F(IoUringImplTest, PrepareReadvAllDataFitsOneChunk) {
  std::string test_file =
      TestEnvironment::writeStringToFileForTest("prepare_readv", "test text", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

  auto dispatcher = api_->allocateDispatcher("test_thread");

  uint8_t buffer[4096]{};
  struct iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = 4096;

  os_fd_t event_fd = io_uring_->registerEventfd();

  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr, d = dispatcher.get()](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](Request*, int32_t res, bool) {
          completions_nr++;
          EXPECT_EQ(res, strlen("test text"));
        });
        d->exit();
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  io_uring_->prepareReadv(fd, &iov, 1, 0, nullptr);
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "");
  io_uring_->submit();

  // Check that the completion callback has been actually called.
  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 1; });
  // The file's content is in the read buffer now.
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "test text");
}

TEST_F(IoUringImplTest, PrepareReadvQueueOverflow) {
  std::string test_file =
      TestEnvironment::writeStringToFileForTest("prepare_readv_overflow", "abcdefhg", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

  auto dispatcher = api_->allocateDispatcher("test_thread");

  uint8_t buffer1[2]{};
  struct iovec iov1;
  iov1.iov_base = buffer1;
  iov1.iov_len = 2;
  uint8_t buffer2[2]{};
  struct iovec iov2;
  iov2.iov_base = buffer2;
  iov2.iov_len = 2;
  uint8_t buffer3[2]{};
  struct iovec iov3;
  iov3.iov_base = buffer3;
  iov3.iov_len = 2;

  os_fd_t event_fd = io_uring_->registerEventfd();
  const Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  int32_t completions_nr = 0;
  auto file_event = dispatcher->createFileEvent(
      event_fd,
      [this, &completions_nr](uint32_t) {
        io_uring_->forEveryCompletion([&completions_nr](Request* user_data, int32_t res, bool) {
          EXPECT_TRUE(user_data != nullptr);
          EXPECT_EQ(res, 2);
          completions_nr++;
          // Note: generally events are not guaranteed to complete in the same order
          // we submit them, but for this case of reading from a single file it's ok
          // to expect the same order.
          EXPECT_EQ(dynamic_cast<TestRequest*>(user_data)->data_, completions_nr);
        });
        return absl::OkStatus();
      },
      trigger, Event::FileReadyType::Read);

  int data1 = 1;
  TestRequest request1(data1);
  IoUringResult res = io_uring_->prepareReadv(fd, &iov1, 1, 0, &request1);
  EXPECT_EQ(res, IoUringResult::Ok);
  int data2 = 2;
  TestRequest request2(data2);
  res = io_uring_->prepareReadv(fd, &iov2, 1, 2, &request2);
  EXPECT_EQ(res, IoUringResult::Ok);
  int data3 = 3;
  TestRequest request3(data3);
  res = io_uring_->prepareReadv(fd, &iov3, 1, 4, &request3);
  // Expect the submission queue overflow.
  EXPECT_EQ(res, IoUringResult::Failed);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 2; });
  // Even though we haven't been notified about ops completion the buffers
  // are filled already.
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[0], 'a');
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[1], 'b');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[0], 'c');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[1], 'd');

  // Only 2 completions are expected because the completion queue can contain
  // no more than 2 entries.
  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 2; });

  // Check a new event gets handled in the next dispatcher run.
  res = io_uring_->prepareReadv(fd, &iov3, 1, 4, &request3);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = io_uring_->submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  waitForCondition(*dispatcher, [&completions_nr]() { return completions_nr == 3; });

  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[0], 'e');
  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[1], 'f');
}

} // namespace
} // namespace Io
} // namespace Envoy
