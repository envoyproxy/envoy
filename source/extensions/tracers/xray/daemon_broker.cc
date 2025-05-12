#include "source/extensions/tracers/xray/daemon_broker.h"

#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/tracers/xray/daemon.pb.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

namespace {
// creates a header JSON for X-Ray daemon.
// For example:
// { "format": "json", "version": 1}
std::string createHeader(const std::string& format, uint32_t version) {
  source::extensions::tracers::xray::daemon::Header header;
  header.set_format(format);
  header.set_version(version);
  return MessageUtil::getJsonStringFromMessageOrError(header, false /* pretty_print  */,
                                                      false /* always_print_primitive_fields */);
}

} // namespace

DaemonBrokerImpl::DaemonBrokerImpl(const std::string& daemon_endpoint)
    : address_(
          Network::Utility::parseInternetAddressAndPortNoThrow(daemon_endpoint, false /*v6only*/)),
      io_handle_(Network::ioHandleForAddr(Network::Socket::Type::Datagram, address_, {})) {
  if (address_ == nullptr) {
    throw EnvoyException(absl::StrCat("malformed IP address: ", daemon_endpoint));
  }
}

void DaemonBrokerImpl::send(const std::string& data) const {
  auto& logger = Logger::Registry::getLog(Logger::Id::tracing);
  constexpr auto version = 1;
  constexpr auto format = "json";
  const std::string payload = absl::StrCat(createHeader(format, version), "\n", data);
  Buffer::RawSlice buf;
  buf.mem_ = const_cast<char*>(payload.data());
  buf.len_ = payload.length();
  const auto rc = Network::Utility::writeToSocket(*io_handle_, &buf, 1 /*num_slices*/,
                                                  nullptr /*local_ip*/, *address_);

  if (rc.return_value_ != payload.length()) {
    // TODO(suniltheta): report this in stats
    ENVOY_LOG_TO_LOGGER(logger, debug, "Failed to send trace payload to the X-Ray daemon.");
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
