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

  MOCK_METHOD(os_fd_t, fdDoNotUse, (), (const, override));
  MOCK_METHOD(void, setAbortiveClose, (), (override));
  MOCK_METHOD(Api::IoCallUint64Result, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(bool, wasConnected, (), (const, override));
  MOCK_METHOD(Api::IoCallUint64Result, readv,
              (uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice), (override));
  MOCK_METHOD(Api::IoCallUint64Result, read,
              (Buffer::Instance & buffer, std::optional<uint64_t> max_length), (override));
  MOCK_METHOD(Api::IoCallUint64Result, writev, (const Buffer::RawSlice* slices, uint64_t num_slice),
              (override));
  MOCK_METHOD(Api::IoCallUint64Result, write, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(Api::IoCallUint64Result, send, (const void* buffer, size_t length), (override));
  MOCK_METHOD(Api::IoCallUint64Result, sendmsg,
              (const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
               const Address::Ip* self_ip, const Address::Instance& peer_address),
              (override));
  MOCK_METHOD(Api::IoCallUint64Result, recvmsg,
              (Buffer::RawSlice * slices, const uint64_t num_slice, uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output),
              (override));
  MOCK_METHOD(Api::IoCallUint64Result, recvmmsg,
              (RawSliceArrays & slices, uint32_t self_port,
               const UdpSaveCmsgConfig& save_cmsg_config, RecvMsgOutput& output),
              (override));
  MOCK_METHOD(Api::IoCallUint64Result, recv, (void* buffer, size_t length, int flags), (override));
  MOCK_METHOD(bool, supportsMmsg, (), (const, override));
  MOCK_METHOD(bool, supportsUdpGro, (), (const, override));
  MOCK_METHOD(Api::SysCallIntResult, bind, (Address::InstanceConstSharedPtr address), (override));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int backlog), (override));
  MOCK_METHOD(IoHandlePtr, accept, (struct sockaddr * addr, socklen_t* addrlen), (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (Address::InstanceConstSharedPtr address),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, setOption,
              (int level, int optname, const void* optval, socklen_t optlen), (override));
  MOCK_METHOD(Api::SysCallIntResult, getOption,
              (int level, int optname, void* optval, socklen_t* optlen), (override));
  MOCK_METHOD(Api::SysCallIntResult, setBlocking, (bool blocking), (override));
  MOCK_METHOD(std::optional<int>, domain, (), (override));
  MOCK_METHOD(absl::StatusOr<Address::InstanceConstSharedPtr>, localAddress, (), (override));
  MOCK_METHOD(absl::StatusOr<Address::InstanceConstSharedPtr>, peerAddress, (), (override));
  MOCK_METHOD(IoHandlePtr, duplicate, (), (override));
  MOCK_METHOD(void, createFileEvent_,
              (Event::Dispatcher & dispatcher, Event::FileReadyCb cb,
               Event::FileTriggerType trigger, uint32_t events));
  MOCK_METHOD(void, activateFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, enableFileEvents, (uint32_t events), (override));
  MOCK_METHOD(void, resetFileEvents, (), (override));
  MOCK_METHOD(Api::SysCallIntResult, shutdown, (int how), (override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, lastRoundTripTime, (), (override));
  MOCK_METHOD(std::optional<uint64_t>, congestionWindowInBytes, (), (const, override));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*),
              (override));
  MOCK_METHOD(std::optional<std::string>, interfaceName, (), (override));
};

} // namespace Network
} // namespace Envoy
