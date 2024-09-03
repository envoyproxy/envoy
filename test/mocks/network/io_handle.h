#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockIoHandle : public IoHandle {
public:
  MockIoHandle();
  ~MockIoHandle() override;

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override {
    createFileEvent_(dispatcher, cb, trigger, events);
  }

  MOCK_METHOD(os_fd_t, fdDoNotUse, (), (const));
  MOCK_METHOD(Api::IoCallUint64Result, close, ());
  MOCK_METHOD(bool, isOpen, (), (const));
  MOCK_METHOD(bool, wasConnected, (), (const));
  MOCK_METHOD(Api::IoCallUint64Result, readv,
              (uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(Api::IoCallUint64Result, read,
              (Buffer::Instance & buffer, absl::optional<uint64_t> max_length));
  MOCK_METHOD(Api::IoCallUint64Result, writev,
              (const Buffer::RawSlice* slices, uint64_t num_slice));
  MOCK_METHOD(Api::IoCallUint64Result, write, (Buffer::Instance & buffer));
  MOCK_METHOD(Api::IoCallUint64Result, sendmsg,
              (const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
               const Address::Ip* self_ip, const Address::Instance& peer_address));
  MOCK_METHOD(Api::IoCallUint64Result, recvmsg,
              (Buffer::RawSlice * slices, const uint64_t num_slice, uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output));
  MOCK_METHOD(Api::IoCallUint64Result, recvmmsg,
              (RawSliceArrays & slices, uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output));
  MOCK_METHOD(Api::IoCallUint64Result, recv, (void* buffer, size_t length, int flags));
  MOCK_METHOD(bool, supportsMmsg, (), (const));
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
  MOCK_METHOD(Api::SysCallIntResult, bind, (Address::InstanceConstSharedPtr address));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int backlog));
  MOCK_METHOD(IoHandlePtr, accept, (struct sockaddr * addr, socklen_t* addrlen));
  MOCK_METHOD(Api::SysCallIntResult, connect, (Address::InstanceConstSharedPtr address));
  MOCK_METHOD(Api::SysCallIntResult, setOption,
              (int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD(Api::SysCallIntResult, getOption,
              (int level, int optname, void* optval, socklen_t* optlen));
  MOCK_METHOD(Api::SysCallIntResult, setBlocking, (bool blocking));
  MOCK_METHOD(absl::optional<int>, domain, ());
  MOCK_METHOD(Address::InstanceConstSharedPtr, localAddress, ());
  MOCK_METHOD(Address::InstanceConstSharedPtr, peerAddress, ());
  MOCK_METHOD(IoHandlePtr, duplicate, ());
  MOCK_METHOD(void, createFileEvent_,
              (Event::Dispatcher & dispatcher, Event::FileReadyCb cb,
               Event::FileTriggerType trigger, uint32_t events));
  MOCK_METHOD(void, activateFileEvents, (uint32_t events));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events));
  MOCK_METHOD(void, resetFileEvents, ());
  MOCK_METHOD(Api::SysCallIntResult, shutdown, (int how));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, lastRoundTripTime, ());
  MOCK_METHOD(absl::optional<uint64_t>, congestionWindowInBytes, (), (const));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*));
  MOCK_METHOD(absl::optional<std::string>, interfaceName, ());
};

} // namespace Network
} // namespace Envoy
