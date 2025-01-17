#include "test/integration/socket_interface_swap.h"

namespace Envoy {

SocketInterfaceSwap::SocketInterfaceSwap(Network::Socket::Type socket_type)
    : write_matcher_(std::make_shared<IoHandleMatcher>(socket_type)),
      test_socket_interface_loader_(std::make_unique<Envoy::Network::TestSocketInterface>(
          [write_matcher =
               write_matcher_](Envoy::Network::TestIoSocketHandle* io_handle,
                               Network::Address::InstanceConstSharedPtr& peer_address_override_out)
              -> absl::optional<Api::IoCallUint64Result> {
            Api::IoErrorPtr error_override =
                write_matcher->returnConnectOverride(io_handle, peer_address_override_out);
            if (error_override) {
              return Api::IoCallUint64Result(0, std::move(error_override));
            }
            return absl::nullopt;
          },
          [write_matcher = write_matcher_](
              Envoy::Network::TestIoSocketHandle* io_handle, const Buffer::RawSlice*, uint64_t,
              Network::Address::InstanceConstSharedPtr& peer_address_override_out)
              -> absl::optional<Api::IoCallUint64Result> {
            Api::IoErrorPtr error_override =
                write_matcher->returnOverride(io_handle, peer_address_override_out);
            if (error_override) {
              return Api::IoCallUint64Result(0, std::move(error_override));
            }
            return absl::nullopt;
          },
          [write_matcher = write_matcher_](Network::IoHandle::RecvMsgOutput& output) {
            write_matcher->readOverride(output);
          })) {}

void SocketInterfaceSwap::IoHandleMatcher::setResumeWrites() {
  absl::MutexLock lock(&mutex_);
  mutex_.Await(absl::Condition(
      +[](Network::TestIoSocketHandle** matched_iohandle) { return *matched_iohandle != nullptr; },
      &matched_iohandle_));
  error_ = nullptr;
  matched_iohandle_->activateInDispatcherThread(Event::FileReadyType::Write);
}

} // namespace Envoy
