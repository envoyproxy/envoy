#include <sys/epoll.h>

#include "source/common/io/io_uring_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

class IoUringBaseTest : public ::testing::Test {
public:
  void TearDown() override {
    auto& uring = factory_.getOrCreate();
    if (uring.isEventfdRegistered()) {
      uring.unregisterEventfd();
    }
  }

  static const IoUringFactoryImpl factory_;
};

const IoUringFactoryImpl IoUringBaseTest::factory_(2, false);

class IoUringImplParamTest
    : public IoUringBaseTest,
      public testing::WithParamInterface<std::function<IoUringResult(IoUring&, os_fd_t)>> {};

INSTANTIATE_TEST_SUITE_P(InvalidPrepareMethodParamsTest, IoUringImplParamTest,
                         testing::Values(
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareAccept(fd, nullptr, nullptr, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               auto address =
                                   std::make_shared<Network::Address::EnvoyInternalInstance>(
                                       "test");
                               return uring.prepareConnect(fd, address, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareReadv(fd, nullptr, 0, 0, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareWritev(fd, nullptr, 0, 0, nullptr);
                             },
                             [](IoUring& uring, os_fd_t fd) -> IoUringResult {
                               return uring.prepareClose(fd, nullptr);
                             }));

TEST_P(IoUringImplParamTest, InvalidParams) {
  os_fd_t fd;
  SET_SOCKET_INVALID(fd);

  auto& uring = factory_.getOrCreate();

  os_fd_t event_fd = uring.registerEventfd();
  os_fd_t epoll_fd = epoll_create1(0);
  ASSERT_FALSE(epoll_fd == -1);
  struct epoll_event ev, events[10];
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = event_fd;
  ASSERT_FALSE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev) == -1);

  auto prepare_method = GetParam();
  IoUringResult res = prepare_method(uring, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(uring, fd);
  EXPECT_EQ(res, IoUringResult::Ok);
  res = prepare_method(uring, fd);
  EXPECT_EQ(res, IoUringResult::Failed);
  res = uring.submit();
  EXPECT_EQ(res, IoUringResult::Ok);
  res = uring.submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  int ret = epoll_wait(epoll_fd, events, 10, -1);
  EXPECT_EQ(ret, 1);

  int32_t completions_nr = 0;
  uring.forEveryCompletion([&completions_nr](void*, int32_t res) {
    EXPECT_TRUE(res < 0);
    completions_nr++;
  });
  EXPECT_EQ(completions_nr, 2);
}

class IoUringImplTest : public IoUringBaseTest {
protected:
  void SetUp() override { test_dir_ = TestEnvironment::temporaryDirectory(); }

  void TearDown() override {
    TestEnvironment::removePath(test_dir_);
    IoUringBaseTest::TearDown();
  }

  std::string test_dir_;
};

TEST_F(IoUringImplTest, Instantiate) {
  auto& uring1 = factory_.getOrCreate();
  auto& uring2 = factory_.getOrCreate();
  EXPECT_EQ(&uring1, &uring2);

  EXPECT_DEATH(IoUringFactoryImpl factory2(10, false),
               "only one io_uring per thread is supported now");
  EXPECT_DEATH(IoUringFactoryImpl factory3(20, true),
               "only one io_uring per thread is supported now");
}

TEST_F(IoUringImplTest, RegisterEventfd) {
  auto& uring = factory_.getOrCreate();

  EXPECT_FALSE(uring.isEventfdRegistered());
  uring.registerEventfd();
  EXPECT_TRUE(uring.isEventfdRegistered());
  uring.unregisterEventfd();
  EXPECT_FALSE(uring.isEventfdRegistered());
  EXPECT_DEATH(uring.unregisterEventfd(), "unable to unregister eventfd");
}

TEST_F(IoUringImplTest, PrepareReadvAllDataFitsOneChunk) {
  std::string test_file = TestEnvironment::writeStringToFileForTest(
      absl::StrCat(test_dir_, "prepare_readv"), "test text", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

  uint8_t buffer[4096]{};
  struct iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = 4096;

  auto& uring = factory_.getOrCreate();
  os_fd_t event_fd = uring.registerEventfd();
  os_fd_t epoll_fd = epoll_create1(0);
  ASSERT_FALSE(epoll_fd == -1);
  struct epoll_event ev, events[10];
  ev.events = EPOLLIN;
  ev.data.fd = event_fd;
  ASSERT_FALSE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev) == -1);
  uring.prepareReadv(fd, &iov, 1, 0, nullptr);
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "");
  uring.submit();
  int ret = epoll_wait(epoll_fd, events, 10, -1);
  EXPECT_EQ(ret, 1);
  EXPECT_STREQ(static_cast<char*>(iov.iov_base), "test text");

  uint32_t completions_nr = 0;
  uring.forEveryCompletion([&completions_nr](void*, int32_t res) {
    completions_nr++;
    EXPECT_EQ(res, strlen("test text"));
  });
  EXPECT_EQ(completions_nr, 1);
}

TEST_F(IoUringImplTest, PrepareReadvQueueOverflow) {
  std::string test_file = TestEnvironment::writeStringToFileForTest(
      absl::StrCat(test_dir_, "prepare_readv"), "abcdefhg", true);
  os_fd_t fd = open(test_file.c_str(), O_RDONLY);
  ASSERT_TRUE(fd >= 0);

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

  auto& uring = factory_.getOrCreate();

  os_fd_t event_fd = uring.registerEventfd();
  os_fd_t epoll_fd = epoll_create1(0);
  ASSERT_FALSE(epoll_fd == -1);
  struct epoll_event ev, events[10];
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = event_fd;
  ASSERT_FALSE(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev) == -1);

  IoUringResult res = uring.prepareReadv(fd, &iov1, 1, 0, reinterpret_cast<void*>(1));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = uring.prepareReadv(fd, &iov2, 1, 2, reinterpret_cast<void*>(2));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = uring.prepareReadv(fd, &iov3, 1, 4, reinterpret_cast<void*>(3));
  // Expect the submission queue overflow.
  EXPECT_EQ(res, IoUringResult::Failed);
  res = uring.submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  // Even though we haven't been notified about ops completion the buffers
  // are filled already.
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[0], 'a');
  EXPECT_EQ(static_cast<char*>(iov1.iov_base)[1], 'b');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[0], 'c');
  EXPECT_EQ(static_cast<char*>(iov2.iov_base)[1], 'd');

  int ret = epoll_wait(epoll_fd, events, 10, -1);
  EXPECT_EQ(ret, 1);

  uint32_t completions_nr = 0;
  uring.forEveryCompletion([&completions_nr](void* user_data, int32_t res) {
    EXPECT_TRUE(user_data != nullptr);
    EXPECT_TRUE(reinterpret_cast<int64_t>(user_data) < 3);
    EXPECT_EQ(res, 2);
    completions_nr++;
  });
  // Only 2 completions are expected because the completion queue can contain
  // no more than 2 entries.
  EXPECT_EQ(completions_nr, 2);

  res = uring.prepareReadv(fd, &iov3, 1, 4, reinterpret_cast<void*>(3));
  EXPECT_EQ(res, IoUringResult::Ok);
  res = uring.submit();
  EXPECT_EQ(res, IoUringResult::Ok);

  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[0], 'e');
  EXPECT_EQ(static_cast<char*>(iov3.iov_base)[1], 'f');

  ret = epoll_wait(epoll_fd, events, 10, -1);
  EXPECT_EQ(ret, 1);

  completions_nr = 0;
  uring.forEveryCompletion([&completions_nr](void* user_data, int32_t res) {
    EXPECT_EQ(reinterpret_cast<int64_t>(user_data), 3);
    EXPECT_EQ(res, 2);
    completions_nr++;
  });
  EXPECT_EQ(completions_nr, 1);
}

} // namespace
} // namespace Io
} // namespace Envoy
