#pragma once

#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/event/dispatcher.h"

#include "common/api/os_sys_calls_impl.h"

#include "test/mocks/filesystem/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Api {

class MockApi : public Api {
public:
  MockApi();
  ~MockApi();

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override {
    return Event::DispatcherPtr{allocateDispatcher_()};
  }

  MOCK_METHOD0(allocateDispatcher_, Event::Dispatcher*());
  MOCK_METHOD4(createFile,
               Filesystem::FileSharedPtr(const std::string& path, Event::Dispatcher& dispatcher,
                                         Thread::BasicLockable& lock, Stats::Store& stats_store));
  MOCK_METHOD1(fileExists, bool(const std::string& path));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string& path));

  std::shared_ptr<Filesystem::MockFile> file_{new Filesystem::MockFile()};
};

class MockOsSysCalls : public OsSysCallsImpl {
public:
  MockOsSysCalls();
  ~MockOsSysCalls();

  // Api::OsSysCalls
  ssize_t write(int fd, const void* buffer, size_t num_bytes) override;
  int open(const std::string& full_path, int flags, int mode) override;
  int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) override;
  int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) override;

  MOCK_METHOD3(bind, int(int sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD1(close, int(int));
  MOCK_METHOD3(open_, int(const std::string& full_path, int flags, int mode));
  MOCK_METHOD3(write_, ssize_t(int, const void*, size_t));
  MOCK_METHOD3(shmOpen, int(const char*, int, mode_t));
  MOCK_METHOD1(shmUnlink, int(const char*));
  MOCK_METHOD2(ftruncate, int(int fd, off_t length));
  MOCK_METHOD6(mmap, void*(void* addr, size_t length, int prot, int flags, int fd, off_t offset));
  MOCK_METHOD2(stat, int(const char* name, struct stat* stat));
  MOCK_METHOD5(setsockopt_,
               int(int sockfd, int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD5(getsockopt_,
               int(int sockfd, int level, int optname, void* optval, socklen_t* optlen));

  size_t num_writes_;
  size_t num_open_;
  Thread::MutexBasicLockable write_mutex_;
  Thread::MutexBasicLockable open_mutex_;
  std::condition_variable_any write_event_;
  std::condition_variable_any open_event_;
  // Map from (sockfd,level,optname) to boolean socket option.
  using SockOptKey = std::tuple<int, int, int>;
  std::map<SockOptKey, bool> boolsockopts_;
};

} // namespace Api
} // namespace Envoy
