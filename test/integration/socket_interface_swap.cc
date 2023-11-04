#include "test/integration/socket_interface_swap.h"

namespace Envoy {

SocketInterfaceSwap::SocketInterfaceSwap(Network::Socket::Type socket_type)
    : write_matcher_(std::make_shared<IoHandleMatcher>(socket_type)) {
  Envoy::Network::SocketInterfaceSingleton::clear();
  test_socket_interface_loader_ = std::make_unique<Envoy::Network::SocketInterfaceLoader>(
      std::make_unique<Envoy::Network::TestSocketInterface>(
          [write_matcher = write_matcher_](
              Envoy::Network::TestIoSocketHandle* io_handle, const Buffer::RawSlice* slices,
              uint64_t size) -> absl::optional<Api::IoCallUint64Result> {
            // TODO(yanavlasov): refactor into separate method after CVE is public.
            if (slices == nullptr && size == 0) {
              // This is connect override check
              Api::IoErrorPtr error_override = write_matcher->returnConnectOverride(io_handle);
              if (error_override) {
                return Api::IoCallUint64Result(0, std::move(error_override));
              }
            } else {
              Api::IoErrorPtr error_override = write_matcher->returnOverride(io_handle);
              if (error_override) {
                return Api::IoCallUint64Result(0, std::move(error_override));
              }
            }
            return absl::nullopt;
          }));
}

void SocketInterfaceSwap::IoHandleMatcher::setResumeWrites() {
  absl::MutexLock lock(&mutex_);
  mutex_.Await(absl::Condition(
      +[](Network::TestIoSocketHandle** matched_iohandle) { return *matched_iohandle != nullptr; },
      &matched_iohandle_));
  error_ = nullptr;
  matched_iohandle_->activateInDispatcherThread(Event::FileReadyType::Write);
}

} // namespace Envoy
