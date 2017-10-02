#pragma once

#include "envoy/api/os_sys_calls.h"

namespace Envoy {
namespace Api {

class OsSysCallsImpl : public OsSysCalls {
public:
  // Api::OsSysCalls
  int open(const std::string& full_path, int flags, int mode) override;
  ssize_t write(int fd, const void* buffer, size_t num_bytes) override;
  int close(int fd) override;
};

} // namespace Api
} // namespace Envoy
