#include "test/integration/filters/test_socket_interface.h"

#include <algorithm>

#include "envoy/common/exception.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"
#include "envoy/network/socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

Api::IoCallUint64Result TestIoSocketHandle::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  if (writev_override_) {
    auto result = writev_override_(index_, slices, num_slice);
    if (result.has_value()) {
      return std::move(result).value();
    }
  }
  return IoSocketHandleImpl::writev(slices, num_slice);
}

TestSocketInterface::TestSocketInterface()
    : previous_socket_interface_(SocketInterfaceSingleton::getExisting()) {
  SocketInterfaceSingleton::clear();
  SocketInterfaceSingleton::initialize(this);
}

TestSocketInterface::~TestSocketInterface() {
  SocketInterfaceSingleton::clear();
  if (previous_socket_interface_) {
    SocketInterfaceSingleton::initialize(previous_socket_interface_);
  }
}

void TestSocketInterface::overrideWritev(TestIoSocketHandle::WritevOverrideProc writev) {
  writev_overide_proc_ = writev;
}

IoHandlePtr TestSocketInterface::socket(os_fd_t fd) {
  return std::make_unique<TestIoSocketHandle>(getAcceptedSocketIndex(), writev_overide_proc_, fd);
}

ProtobufTypes::MessagePtr TestSocketInterface::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::network::socket_interface::v3::DefaultSocketInterface>();
}

} // namespace Network
} // namespace Envoy
