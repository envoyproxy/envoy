#pragma once

#include "envoy/server/hot_restart.h"

namespace Envoy {
namespace Server {

class OsSysCallsImpl : public OsSysCalls {
  // Server::OsSysCalls
  int shmOpen(const char* name, int oflag, mode_t mode) override;
  int shmUnlink(const char* name) override;
  int ftruncate(int fd, off_t length) override;
  void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) override;
};

} // namespace Server
} // namespace Envoy
