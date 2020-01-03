#pragma once

#include "envoy/network/io_handle.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockIoHandle : public IoHandle {
public:
  MockIoHandle();
  ~MockIoHandle();

  MOCK_CONST_METHOD0(fd, int());
  MOCK_METHOD0(close, Api::IoCallUint64Result());
  MOCK_CONST_METHOD0(isOpen, bool());
  MOCK_METHOD3(readv, Api::IoCallUint64Result(uint64_t max_length, Buffer::RawSlice* slices,
                                              uint64_t num_slice));
  MOCK_METHOD2(writev, Api::IoCallUint64Result(const Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD5(sendmsg, Api::IoCallUint64Result(const Buffer::RawSlice* slices, uint64_t num_slice,
                                                int flags, const Address::Ip* self_ip,
                                                const Address::Instance& peer_address));
  MOCK_METHOD4(recvmsg, Api::IoCallUint64Result(Buffer::RawSlice* slices, const uint64_t num_slice,
                                                uint32_t self_port, RecvMsgOutput& output));
};

} // namespace Network
} // namespace Envoy
