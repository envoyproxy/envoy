#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockIoHandle : public IoHandle {
public:
  MockIoHandle();
  ~MockIoHandle();

  MOCK_METHOD(int, fd, (), (const));
  MOCK_METHOD(Api::IoCallUint64Result, close, ());
  MOCK_METHOD(bool, isOpen, (), (const));
  MOCK_METHOD(Api::IoCallUint64Result, readv,
              (uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(Api::IoCallUint64Result, writev,
              (const Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(Api::IoCallUint64Result, sendmsg,
              (const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
               const Address::Ip* self_ip, const Address::Instance& peer_address));
  MOCK_METHOD(Api::IoCallUint64Result, recvmsg,
              (Buffer::RawSlice * slices, const uint64_t num_slice, uint32_t self_port,
               RecvMsgOutput& output));
  MOCK_METHOD(Api::IoCallUint64Result, recvmmsg,
              (RawSliceArrays & slices, uint32_t self_port, RecvMsgOutput& output));
  MOCK_METHOD(bool, supportsMmsg, (), (const));
};

} // namespace Network
} // namespace Envoy
