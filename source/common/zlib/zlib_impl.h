
//include/envoy/zlib_impl.h

#pragma once

#include "envoy/zlib/zlib.h"

#include "zlib.h"

namespace Envoy {
namespace Zlib {

class Impl : public Zlib {
public:
  Impl();
  bool deflateData(Buffer::Instance& data) override;
  uint64_t getTotalIn();
  uint64_t getTotalOut();

private:
  std::unique_ptr<z_stream> ZstreamPtr_{nullptr};
  Buffer::InstancePtr buffer_{nullptr};
  uint64_t total_in_{};
  uint64_t total_out_{};
};

} // namespace zlib
} // namespace envoy