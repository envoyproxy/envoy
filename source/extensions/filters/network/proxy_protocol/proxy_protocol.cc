#include "extensions/filters/network/proxy_protocol/proxy_protocol.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/tcp_proxy/tcp_proxy.h"

#include "extensions/filters/common/proxy_protocol/proxy_protocol_header.h"

using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V1_AF_INET6;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V1_SIGNATURE;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET6;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET6;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_ONBEHALF_OF;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE_LEN;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_TRANSPORT_STREAM;
using Envoy::Extensions::Filters::Common::ProxyProtocol::PROXY_PROTO_V2_VERSION;
using envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol_Version;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ProxyProtocol {

Filter::Filter(const envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol& config)
    : config_(config) {}

Network::FilterStatus Filter::onWrite(Buffer::Instance&, bool) {
  if (!sent_header_) {
    injectHeader();
    sent_header_ = true;
  }
  return Network::FilterStatus::Continue;
}

void Filter::injectHeader() {
  if (config_.version() == ProxyProtocol_Version::ProxyProtocol_Version_V1) {
    injectHeaderV1();
  } else if (config_.version() == ProxyProtocol_Version::ProxyProtocol_Version_V2) {
    injectHeaderV2();
  }
}

void Filter::injectHeaderV1() {
  std::ostringstream stream;
  stream << PROXY_PROTO_V1_SIGNATURE;
  // Default to local addresses
  std::string src_address = write_callbacks_->connection().localAddress()->ip()->addressAsString();
  std::string dst_address = write_callbacks_->connection().remoteAddress()->ip()->addressAsString();
  auto src_port = write_callbacks_->connection().localAddress()->ip()->port();
  auto dstPort = write_callbacks_->connection().remoteAddress()->ip()->port();
  auto ip_version = write_callbacks_->connection().localAddress()->ip()->version();

  if (write_callbacks_->connection().streamInfo().filterState()->hasData<TcpProxy::DownstreamAddrs>(
          TcpProxy::DownstreamAddrs::key())) {
    const auto downAddrs =
        write_callbacks_->connection()
            .streamInfo()
            .filterState()
            ->getDataReadOnly<TcpProxy::DownstreamAddrs>(TcpProxy::DownstreamAddrs::key());
    ip_version = downAddrs.version();
    src_address = downAddrs.srcAddress();
    dst_address = downAddrs.dstAddress();
    src_port = downAddrs.srcPort();
    dstPort = downAddrs.dstPort();
  }

  switch (ip_version) {
  case Network::Address::IpVersion::v4:
    stream << PROXY_PROTO_V1_AF_INET << " ";
    break;
  case Network::Address::IpVersion::v6:
    stream << PROXY_PROTO_V1_AF_INET6 << " ";
    break;
  }

  stream << src_address << " ";
  stream << dst_address << " ";
  stream << src_port << " ";
  stream << dstPort << "\r\n";

  Buffer::OwnedImpl buff(stream.str());
  write_callbacks_->injectWriteDataToFilterChain(buff, false);
}

void Filter::injectHeaderV2() {
  Buffer::OwnedImpl buff(PROXY_PROTO_V2_SIGNATURE, PROXY_PROTO_V2_SIGNATURE_LEN);

  if (!write_callbacks_->connection()
           .streamInfo()
           .filterState()
           ->hasData<TcpProxy::DownstreamAddrs>(TcpProxy::DownstreamAddrs::key())) {
    // local command
    const uint8_t addr_fam_protocol_and_length[4]{PROXY_PROTO_V2_VERSION << 4, 0, 0, 0};
    buff.add(addr_fam_protocol_and_length, 4);
    write_callbacks_->injectWriteDataToFilterChain(buff, false);
    return;
  }

  const auto down_addrs =
      write_callbacks_->connection()
          .streamInfo()
          .filterState()
          ->getDataReadOnly<TcpProxy::DownstreamAddrs>(TcpProxy::DownstreamAddrs::key());

  const uint8_t version_and_command = PROXY_PROTO_V2_VERSION << 4 | PROXY_PROTO_V2_ONBEHALF_OF;
  buff.add(&version_and_command, 1);

  uint8_t address_family_and_protocol;
  switch (down_addrs.version()) {
  case Network::Address::IpVersion::v4:
    address_family_and_protocol = PROXY_PROTO_V2_AF_INET << 4;
    break;
  case Network::Address::IpVersion::v6:
    address_family_and_protocol = PROXY_PROTO_V2_AF_INET6 << 4;
    break;
  }
  address_family_and_protocol |= PROXY_PROTO_V2_TRANSPORT_STREAM;
  buff.add(&address_family_and_protocol, 1);

  uint8_t addr_length[2]{0, 0};
  switch (down_addrs.version()) {
  case Network::Address::IpVersion::v4: {
    addr_length[1] = PROXY_PROTO_V2_ADDR_LEN_INET;
    buff.add(addr_length, 2);

    uint8_t addrs[8];
    const auto down_remote_addr =
        Network::Address::Ipv4Instance(down_addrs.srcAddress(), down_addrs.srcPort())
            .ip()
            ->ipv4()
            ->address();
    const auto down_local_addr =
        Network::Address::Ipv4Instance(down_addrs.dstAddress(), down_addrs.dstPort())
            .ip()
            ->ipv4()
            ->address();
    memcpy(addrs, &down_remote_addr, 4);
    memcpy(&addrs[4], &down_local_addr, 4);
    buff.add(addrs, 8);
    break;
  }
  case Network::Address::IpVersion::v6: {
    addr_length[1] = PROXY_PROTO_V2_ADDR_LEN_INET6;
    buff.add(addr_length, 2);

    uint8_t addrs[32];
    const auto down_remote_addr =
        Network::Address::Ipv6Instance(down_addrs.srcAddress(), down_addrs.srcPort())
            .ip()
            ->ipv6()
            ->address();
    const auto down_local_addr =
        Network::Address::Ipv6Instance(down_addrs.dstAddress(), down_addrs.dstPort())
            .ip()
            ->ipv6()
            ->address();
    memcpy(addrs, &down_remote_addr, 16);
    memcpy(&addrs[16], &down_local_addr, 16);
    buff.add(addrs, 32);
    break;
  }
  }

  uint8_t ports[4];
  const auto down_remote_port = htons(static_cast<uint16_t>(down_addrs.srcPort()));
  const auto down_local_port = htons(static_cast<uint16_t>(down_addrs.dstPort()));
  memcpy(ports, &down_remote_port, 2);
  memcpy(&ports[2], &down_local_port, 2);
  buff.add(ports, 4);

  write_callbacks_->injectWriteDataToFilterChain(buff, false);
}

void Filter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

} // namespace ProxyProtocol
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
