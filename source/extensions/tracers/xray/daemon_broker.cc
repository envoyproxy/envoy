#include "extensions/tracers/xray/daemon_broker.h"

#include "common/network/utility.h"

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
  using namespace envoy::tracers::xray;
  daemon::Header header;
  header.set_format(format);
  header.set_version(version);

  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  std::string json;
  const auto status = Protobuf::util::MessageToJsonString(header, &json, json_options);
  if (!status.ok()) {
    throw new EnvoyException("Failed to serialize X-Ray header to JSON.");
  }
  return json;
}

} // namespace

DaemonBrokerImpl::DaemonBrokerImpl(const std::string& daemon_endpoint) {
  using Network::Utility;
  auto& logger = Logger::Registry::getLog(Logger::Id::tracing);

  const auto address = Utility::parseInternetAddressAndPort(daemon_endpoint, false /*v6only*/);
  io_handle_ = address->socket(Network::Address::SocketType::Datagram);
  if (io_handle_->fd() == -1) {
    ENVOY_LOG_TO_LOGGER(logger, error, "Failed to acquire UDP socket to X-Ray daemon at - {}",
                        daemon_endpoint);
  }
  const auto result = address->connect(io_handle_->fd());
  if (result.rc_ == -1) {
    ENVOY_LOG_TO_LOGGER(logger, error, "Failed to send X-Ray UDP packet to - {}", daemon_endpoint);
  }
}

void DaemonBrokerImpl::send(const std::string& data) const {
  const auto payload = fmt::format("{}\n{}", createHeader("json" /*format*/, 1 /*version*/), data);
  ::send(io_handle_->fd(), payload.c_str(), payload.size(), MSG_DONTWAIT);
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
