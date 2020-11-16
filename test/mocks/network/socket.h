#pragma once

#include "envoy/network/socket.h"

#include "test/mocks/network/io_handle.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockSocket : public Socket {
public:
  MockSocket();
  ~MockSocket() override;

  IoHandle& ioHandle() override;
  const IoHandle& ioHandle() const override;
  Api::SysCallIntResult setSocketOption(int level, int optname, const void* optval,
                                        socklen_t len) override;

  MOCK_METHOD(const Address::InstanceConstSharedPtr&, localAddress, (), (const, override));
  MOCK_METHOD(void, setLocalAddress, (const Address::InstanceConstSharedPtr&), (override));
  MOCK_METHOD(Network::SocketPtr, duplicate, (), ());
  MOCK_METHOD(Socket::Type, socketType, (), (const, override));
  MOCK_METHOD(Address::Type, addressType, (), (const, override));
  MOCK_METHOD(absl::optional<Address::IpVersion>, ipVersion, (), (const, override));
  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(bool, isOpen, (), (const, override));
  MOCK_METHOD(const OptionsSharedPtr&, options, (), (const, override));
  MOCK_METHOD(Api::SysCallIntResult, bind, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (const Address::InstanceConstSharedPtr), (override));
  MOCK_METHOD(Api::SysCallIntResult, listen, (int), (override));
  MOCK_METHOD(Api::SysCallIntResult, getSocketOption, (int, int, void*, socklen_t*),
              (const, override));
  MOCK_METHOD(Api::SysCallIntResult, setBlockingForTest, (bool), (override));
  MOCK_METHOD(void, addOption, (const Socket::OptionConstSharedPtr&), (override));
  MOCK_METHOD(void, addOptions, (const Socket::OptionsSharedPtr&), (override));

  const std::unique_ptr<MockIoHandle> io_handle_;
};

} // namespace Network
} // namespace Envoy
