#pragma once

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/io_socket_handle_impl.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

static constexpr int kFakeSocketFd = 42;

class FakeConnectionSocket : public Network::MockConnectionSocket {
public:
  FakeConnectionSocket()
      : io_handle_(std::make_unique<Network::IoSocketHandleImpl>(kFakeSocketFd)) {}

  ~FakeConnectionSocket() override { io_handle_->close(); }

  Network::IoHandle& ioHandle() override;

  const Network::IoHandle& ioHandle() const override;

  Network::Address::Type addressType() const override;

  absl::optional<Network::Address::IpVersion> ipVersion() const override;

  void setRequestedApplicationProtocols(const std::vector<absl::string_view>& protocols) override;

  const std::vector<std::string>& requestedApplicationProtocols() const override;

  void setDetectedTransportProtocol(absl::string_view protocol) override;

  absl::string_view detectedTransportProtocol() const override;

  void setRequestedServerName(absl::string_view server_name) override;

  absl::string_view requestedServerName() const override;

  void setJA3Hash(absl::string_view ja3_hash) override;

  absl::string_view ja3Hash() const override;

  Api::SysCallIntResult getSocketOption(int level, int, void* optval, socklen_t*) const override;

  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;

private:
  const Network::IoHandlePtr io_handle_;
  std::vector<std::string> application_protocols_;
  std::string transport_protocol_;
  std::string server_name_;
  std::string ja3_hash_;
};

// TODO: Move over to Fake (name is confusing)
class FakeOsSysCalls : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(Api::SysCallSizeResult, recv, (os_fd_t, void*, size_t, int));
  MOCK_METHOD(Api::SysCallIntResult, ioctl,
              (os_fd_t, unsigned long, void*, unsigned long, void*, unsigned long, unsigned long*));
};

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
