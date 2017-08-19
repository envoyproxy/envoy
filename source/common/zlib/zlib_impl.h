
//include/envoy/zlib_impl.h

#pragma once

#include "envoy/zlib/zlib.h"

#include "zlib.h"

namespace Envoy {
namespace Zlib {

class Impl : public Zlib {
public:
  Impl();
  bool deflateData(Buffer::Instance& in) override;
  Buffer::Instance& moveOut();
  uint64_t getTotalIn();
  uint64_t getTotalOut();
  void endStream();

private:
  std::unique_ptr<z_stream> ZstreamPtr_{std::unique_ptr<z_stream>(new z_stream())};
  Buffer::InstancePtr buffer_{nullptr};
  Buffer::RawSlice out_slice{};
  const uint CHUNK_{1024};
};

} // namespace zlib
} // namespace envoy