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

TestIoSocketHandle::~TestIoSocketHandle() {
  TestSocketInterface::getMutableSingleton().clearSocket(this);
}

void TestIoSocketHandle::setWritevOverride(WritevOverrideProc override_proc) {
  absl::WriterMutexLock lock(&mutex_);
  writev_override_ = override_proc;
}

Api::IoCallUint64Result TestIoSocketHandle::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  {
    absl::ReaderMutexLock lock(&mutex_);
    if (writev_override_) {
      auto result = writev_override_(slices, num_slice);
      if (result.has_value()) {
        return std::move(result).value();
      }
    }
  }
  return IoSocketHandleImpl::writev(slices, num_slice);
}

TestSocketInterface* TestSocketInterface::singleton_ = nullptr;

IoHandlePtr TestSocketInterface::socket(os_fd_t fd) {
  auto io_handle = std::make_unique<TestIoSocketHandle>(fd);
  addAcceptedSocket(io_handle.get());
  return io_handle;
}

ProtobufTypes::MessagePtr TestSocketInterface::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::network::socket_interface::v3::DefaultSocketInterface>();
}

void TestSocketInterface::addAcceptedSocket(TestIoSocketHandle* handle) {
  absl::WriterMutexLock lock(&mutex_);
  accepted_sockets_.push_back(handle);
}

void TestSocketInterface::clearSocket(TestIoSocketHandle* handle) {
  absl::WriterMutexLock lock(&mutex_);
  auto pos = std::find(accepted_sockets_.begin(), accepted_sockets_.end(), handle);
  ASSERT(pos != accepted_sockets_.end());
  *pos = nullptr;
}

TestIoSocketHandle* TestSocketInterface::waitForAcceptedSocket(uint32_t index) const {
  absl::ReaderMutexLock lock(&mutex_);
  auto has_index = [this, index]() {
    mutex_.AssertReaderHeld();
    return accepted_sockets_.size() > index;
  };
  mutex_.Await(absl::Condition(&has_index));
  return accepted_sockets_[index];
}

void TestSocketInterface::clearAll() {
  absl::WriterMutexLock lock(&mutex_);
  accepted_sockets_.clear();
}

void TestSocketInterface::install() {
  static SocketInterfaceLoader* socket_interface_loader = nullptr;
  if (socket_interface_loader == nullptr) {
    Envoy::Network::SocketInterfaceSingleton::clear();
    auto interface = std::make_unique<TestSocketInterface>();
    ASSERT(singleton_ == nullptr);
    singleton_ = interface.get();
    socket_interface_loader = new SocketInterfaceLoader(std::move(interface));
  } else {
    singleton_->clearAll();
  }
}

} // namespace Network
} // namespace Envoy
