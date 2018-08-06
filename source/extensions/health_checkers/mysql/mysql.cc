#include "extensions/health_checkers/mysql/mysql.h"

#include "envoy/event/dispatcher.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace MySQLHealthChecker {

MySQLHealthChecker::MySQLHealthChecker(
    const Upstream::Cluster& cluster, const envoy::api::v2::core::HealthCheck& config,
    const envoy::config::health_checker::mysql::v2::MySQL& mysql_config,
    Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    Upstream::HealthCheckEventLoggerPtr&& event_logger)
    : Upstream::HealthCheckerImplBase(cluster, config, dispatcher, runtime, random,
                                      std::move(event_logger)),
      user_(mysql_config.user()) {}

MySQLHealthChecker::MySQLActiveHealthCheckSession::~MySQLActiveHealthCheckSession() {
  if (client_) {
    client_->close(Network::ConnectionCloseType::NoFlush);
  }
}

constexpr const auto MYSQL_PROTOCOL_VERSION = 10;

constexpr const auto mysql_payload_length_offset = 0;
constexpr const size_t mysql_payload_length_size = 3;

constexpr const auto mysql_sequence_id_offset =
    mysql_payload_length_offset + mysql_payload_length_size;
constexpr const size_t mysql_sequence_id_size = 1;

constexpr const size_t mysql_packet_header_size = mysql_sequence_id_offset + mysql_sequence_id_size;
constexpr const auto mysql_payload_offset = mysql_packet_header_size;

bool MySQLHealthChecker::MySQLActiveHealthCheckSession::parseMySqlServerGreetingPacket(
    Buffer::Instance& buffer) {
  // parse server greeting packet:
  // https://dev.mysql.com/doc/internals/en/connection-phase-packets.html
  //  #packet-Protocol::Handshake

  // If the packet contains less than the bare minimum information it is malformed
  constexpr const size_t protocol_version_size = 1;
  constexpr const size_t essential_packet_size = mysql_packet_header_size + protocol_version_size;
  if (buffer.length() < essential_packet_size) {
    ENVOY_CONN_LOG(trace,
                   "mysql healthcheck failed [greeting]: packet truncated, expected at least {} "
                   "bytes, got {} ",
                   *client_, essential_packet_size, buffer.length());
    return false;
  }

  const auto payload_length =
      buffer.peekLEInt<size_t, mysql_payload_length_size>(mysql_payload_length_offset);
  if (buffer.length() != mysql_packet_header_size + payload_length) {
    ENVOY_CONN_LOG(
        trace, "mysql healthcheck failed [greeting]: payload length mismatch, expected {}, got {} ",
        *client_, mysql_packet_header_size + payload_length, buffer.length());
    return false;
  }

  const auto sequence_id =
      buffer.peekLEInt<size_t, mysql_sequence_id_size>(mysql_sequence_id_offset);
  if (sequence_id != 0) {
    ENVOY_CONN_LOG(trace, "mysql healthcheck failed [greeting]: invalid sequence id {} ", *client_,
                   sequence_id);
    return false;
  }

  // Handshake packet starts with the protocol version in the first byte. Currently we only
  // support version 10 which has the version number immediately followed by a variable size
  // NUL terminated string.
  const auto protocol_version =
      buffer.peekLEInt<uint8_t, protocol_version_size>(mysql_payload_offset);
  if (protocol_version != MYSQL_PROTOCOL_VERSION) {
    ENVOY_CONN_LOG(trace, "mysql healthcheck failed [greeting]: unsupported protocol version {} ",
                   *client_, protocol_version);
    return false;
  }

  // TODO (marcelo_juchem): limit the search width to discard malformed packets where the NUL
  // byte doesn't exist or appears too far into the packet.
  // `Buffer::search_range(needle, size, start, end)` seems like a useful function to help
  // avoid performance degradation due to malformed packet attacks.
  // See `evbuffer_search_range`.
  const uint8_t nul = '\0';
  constexpr const size_t server_version_offset = mysql_payload_offset + protocol_version_size;
  auto server_version_nul_offset = buffer.search(&nul, 1, server_version_offset);

  // If NUL not found the packet is malformed.
  if (server_version_nul_offset < 0) {
    ENVOY_CONN_LOG(trace, "mysql healthcheck failed [greeting]: malformed packet ", *client_);
    return false;
  }

  // Calculate the amount of data needed in the packet
  const auto payload_header_end = server_version_nul_offset + 1 + 4 + 8 + 1;

  const auto lower_capabilities_offset = payload_header_end;
  constexpr const size_t lower_capabilities_size = 2;

  const size_t minimum_needed_packet_size = lower_capabilities_offset + lower_capabilities_size;
  if (buffer.length() < minimum_needed_packet_size) {
    ENVOY_CONN_LOG(trace,
                   "mysql healthcheck failed [greeting]: packet truncated, expected at least {} "
                   "bytes, got {} ",
                   *client_, minimum_needed_packet_size, buffer.length());
    return false;
  }

  constexpr const uint16_t MYSQL_CAPABILITIES_CLIENT_PROTOCOL_41 = 0x0200;
  const auto lower_capabilities =
      buffer.peekLEInt<uint16_t, lower_capabilities_size>(lower_capabilities_offset);
  if (!(lower_capabilities & MYSQL_CAPABILITIES_CLIENT_PROTOCOL_41)) {
    ENVOY_CONN_LOG(
        trace, "mysql healthcheck failed [greeting]: server does not support 4.1 client protocol ",
        *client_);
    return false;
  }

  return true;
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::writeMySqlLoginRequestPacket(
    Buffer::Instance& buffer) {
  // write login request packet:
  // https://dev.mysql.com/doc/internals/en/connection-phase-packets.html
  //  #packet-Protocol::HandshakeResponse320

  constexpr const uint32_t MYSQL_CAPABILITIES_CLIENT_CAN_DO_41_AUTH = 0x8000;
  constexpr const size_t client_capabilities_size = 2;
  constexpr const size_t max_packets_size = 3;
  const size_t user_size = parent_.user_.size() + 1;
  constexpr const size_t auth_response_size = 1;
  const uint8_t nul = '\0';
  const size_t auth_response = '\0';

  // the extra +1 accounts for the NUL terminator in the user name
  const size_t packet_size =
      client_capabilities_size + max_packets_size + user_size + auth_response_size;
  buffer.writeLEInt<size_t, mysql_payload_length_size>(packet_size);

  constexpr const auto sequence_id = 1;
  buffer.writeLEInt<size_t, mysql_sequence_id_size>(sequence_id);

  constexpr const auto client_capabilities = MYSQL_CAPABILITIES_CLIENT_CAN_DO_41_AUTH;
  buffer.writeLEInt<uint32_t, client_capabilities_size>(client_capabilities);

  constexpr const auto max_packets = 65536;
  buffer.writeLEInt<size_t, max_packets_size>(max_packets);

  buffer.add(parent_.user_.data(), parent_.user_.size());
  buffer.add(&nul, 1);

  buffer.add(&auth_response, auth_response_size);
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::writeMySqlQuitPacket(
    Buffer::Instance& buffer) {
  // write quit packet:
  // https://dev.mysql.com/doc/internals/en/com-quit.html
  //  #packet-COM_QUIT

  constexpr const auto MYSQL_QUIT_COMMAND = 1;
  constexpr const size_t quit_command_size = 1;

  const size_t packet_size = quit_command_size;
  buffer.writeLEInt<size_t, mysql_payload_length_size>(packet_size);

  constexpr const auto sequence_id = 0;
  buffer.writeLEInt<size_t, mysql_sequence_id_size>(sequence_id);

  buffer.writeLEInt<uint8_t, quit_command_size>(MYSQL_QUIT_COMMAND);
}

bool MySQLHealthChecker::MySQLActiveHealthCheckSession::parseMySqlOkPacket(
    Buffer::Instance& buffer) {
  // parse response packet
  // https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html
  constexpr const auto MYSQL_OK_RESPONSE = 0;
  constexpr const size_t ok_response_size = 1;
  constexpr const size_t ok_packet_size_threshold = 7;

  const size_t minimum_needed_packet_size = mysql_packet_header_size + ok_response_size;
  if (buffer.length() < minimum_needed_packet_size) {
    ENVOY_CONN_LOG(trace,
                   "mysql healthcheck failed [response]: packet truncated, expected at least {} "
                   "bytes, got {} ",
                   *client_, minimum_needed_packet_size, buffer.length());
    return false;
  }

  const auto payload_length =
      buffer.peekLEInt<size_t, mysql_payload_length_size>(mysql_payload_length_offset);
  if (buffer.length() != mysql_packet_header_size + payload_length) {
    ENVOY_CONN_LOG(
        trace, "mysql healthcheck failed [response]: payload length mismatch, expected {}, got {} ",
        *client_, mysql_packet_header_size + payload_length, buffer.length());
    return false;
  }

  const auto sequence_id =
      buffer.peekLEInt<size_t, mysql_sequence_id_size>(mysql_sequence_id_offset);
  if (sequence_id != 2) {
    ENVOY_CONN_LOG(trace, "mysql healthcheck failed [response]: invalid sequence id {} ", *client_,
                   sequence_id);
    return false;
  }

  const auto response_code = buffer.peekLEInt<uint8_t, ok_response_size>(mysql_payload_offset);
  if (response_code != MYSQL_OK_RESPONSE || buffer.length() < ok_packet_size_threshold) {
    ENVOY_CONN_LOG(trace,
                   "mysql healthcheck failed [response]: expected an OK ({}) response with minimum "
                   "packet size {{}}, got {} with size {} ",
                   *client_, MYSQL_OK_RESPONSE, ok_packet_size_threshold, response_code,
                   buffer.length());
    return false;
  }

  return true;
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::onData(Buffer::Instance& data) {
  // TODO: support for partial data - there's no guarantee that the packet will arrive in one shot.

  switch (phase_) {
  case Phase::Greeting: {
    if (!parseMySqlServerGreetingPacket(data)) {
      break;
    }

    data.drain(data.length());
    phase_ = Phase::Reply;

    Buffer::OwnedImpl buffer;
    writeMySqlLoginRequestPacket(buffer);
    writeMySqlQuitPacket(buffer);
    client_->write(buffer, false);
    return;
  }

  case Phase::Reply: {
    if (!parseMySqlOkPacket(data)) {
      break;
    }

    phase_ = Phase::Over;
    data.drain(data.length());

    handleSuccess();
    if (!parent_.reuse_connection_) {
      client_->close(Network::ConnectionCloseType::NoFlush);
    }
    return;
  }

  case Phase::Over:
    ENVOY_CONN_LOG(trace, "unexpected data received from mysql host", *client_);
    data.drain(data.length());
    return;
  }

  phase_ = Phase::Over;
  data.drain(data.length());

  handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::ACTIVE);
  client_->close(Network::ConnectionCloseType::NoFlush);
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose && phase_ != Phase::Over) {
    handleFailure(envoy::data::core::v2alpha::HealthCheckFailureType::NETWORK);
  }

  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    parent_.dispatcher_.deferredDelete(std::move(client_));
  }
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::onInterval() {
  phase_ = Phase::Greeting;

  if (!client_) {
    client_ = host_->createHealthCheckConnection(parent_.dispatcher_).connection_;
    session_callbacks_.reset(new MySQLSessionCallbacks(*this));
    client_->addConnectionCallbacks(*session_callbacks_);
    client_->addReadFilter(session_callbacks_);

    client_->connect();
    client_->noDelay(true);
  } else {
    Buffer::OwnedImpl buffer;
    writeMySqlLoginRequestPacket(buffer);
    writeMySqlQuitPacket(buffer);
    client_->write(buffer, false);
  }
}

void MySQLHealthChecker::MySQLActiveHealthCheckSession::onTimeout() {
  client_->close(Network::ConnectionCloseType::NoFlush);
}

} // namespace MySQLHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
