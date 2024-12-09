#include "source/extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

#include <sstream>

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.validate.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

using envoy::config::core::v3::ProxyProtocolConfig;
using envoy::config::core::v3::ProxyProtocolConfig_Version;
using envoy::config::core::v3::ProxyProtocolPassThroughTLVs;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

UpstreamProxyProtocolSocket::UpstreamProxyProtocolSocket(
    Network::TransportSocketPtr&& transport_socket,
    Network::TransportSocketOptionsConstSharedPtr options, ProxyProtocolConfig config,
    const UpstreamProxyProtocolStats& stats)
    : PassthroughSocket(std::move(transport_socket)), options_(options), version_(config.version()),
      stats_(stats),
      pass_all_tlvs_(config.has_pass_through_tlvs() ? config.pass_through_tlvs().match_type() ==
                                                          ProxyProtocolPassThroughTLVs::INCLUDE_ALL
                                                    : false) {
  if (config.has_pass_through_tlvs() &&
      config.pass_through_tlvs().match_type() == ProxyProtocolPassThroughTLVs::INCLUDE) {
    for (const auto& tlv_type : config.pass_through_tlvs().tlv_type()) {
      pass_through_tlvs_.insert(0xFF & tlv_type);
    }
  }
}

void UpstreamProxyProtocolSocket::setTransportSocketCallbacks(
    Network::TransportSocketCallbacks& callbacks) {
  transport_socket_->setTransportSocketCallbacks(callbacks);
  callbacks_ = &callbacks;
}

Network::IoResult UpstreamProxyProtocolSocket::doWrite(Buffer::Instance& buffer, bool end_stream) {
  if (header_buffer_.length() > 0) {
    auto header_res = writeHeader();
    if (header_buffer_.length() == 0 && header_res.action_ == Network::PostIoAction::KeepOpen) {
      auto inner_res = transport_socket_->doWrite(buffer, end_stream);
      return {inner_res.action_, header_res.bytes_processed_ + inner_res.bytes_processed_, false};
    }
    return header_res;
  } else {
    return transport_socket_->doWrite(buffer, end_stream);
  }
}

void UpstreamProxyProtocolSocket::generateHeader() {
  if (version_ == ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1) {
    generateHeaderV1();
  } else {
    generateHeaderV2();
  }
}

void UpstreamProxyProtocolSocket::generateHeaderV1() {
  // Default to local addresses. Used if no downstream connection exists or
  // downstream address info is not set e.g. health checks
  auto src_addr = callbacks_->connection().connectionInfoProvider().localAddress();
  auto dst_addr = callbacks_->connection().connectionInfoProvider().remoteAddress();

  if (options_ && options_->proxyProtocolOptions().has_value()) {
    const auto options = options_->proxyProtocolOptions().value();
    src_addr = options.src_addr_;
    dst_addr = options.dst_addr_;
  }

  Common::ProxyProtocol::generateV1Header(*src_addr->ip(), *dst_addr->ip(), header_buffer_);
}

namespace {
std::string toHex(const Buffer::Instance& buffer) {
  std::string bufferStr = buffer.toString();
  return Hex::encode(reinterpret_cast<uint8_t*>(bufferStr.data()), bufferStr.length());
}
} // namespace

void UpstreamProxyProtocolSocket::generateHeaderV2() {
  if (!options_ || !options_->proxyProtocolOptions().has_value()) {
    Common::ProxyProtocol::generateV2LocalHeader(header_buffer_);
  } else {
    const auto options = options_->proxyProtocolOptions().value();
    if (!Common::ProxyProtocol::generateV2Header(options, header_buffer_, pass_all_tlvs_,
                                                 pass_through_tlvs_, buildCustomTLVs())) {
      // There is a warn log in generateV2Header method.
      stats_.v2_tlvs_exceed_max_length_.inc();
    }

    ENVOY_LOG(trace, "generated proxy protocol v2 header, length: {}, buffer: {}",
              header_buffer_.length(), toHex(header_buffer_));
  }
}

Network::IoResult UpstreamProxyProtocolSocket::writeHeader() {
  Network::PostIoAction action = Network::PostIoAction::KeepOpen;
  uint64_t bytes_written = 0;
  do {
    if (header_buffer_.length() == 0) {
      break;
    }

    Api::IoCallUint64Result result = callbacks_->ioHandle().write(header_buffer_);

    if (result.ok()) {
      ENVOY_CONN_LOG(trace, "write returns: {}", callbacks_->connection(), result.return_value_);
      bytes_written += result.return_value_;
    } else {
      ENVOY_CONN_LOG(trace, "write error: {}", callbacks_->connection(),
                     result.err_->getErrorDetails());
      if (result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
        action = Network::PostIoAction::Close;
      }
      break;
    }
  } while (true);

  return {action, bytes_written, false};
}

void UpstreamProxyProtocolSocket::onConnected() {
  generateHeader();
  transport_socket_->onConnected();
}

UpstreamProxyProtocolSocketFactory::UpstreamProxyProtocolSocketFactory(
    Network::UpstreamTransportSocketFactoryPtr transport_socket_factory, ProxyProtocolConfig config,
    Stats::Scope& scope)
    : PassthroughFactory(std::move(transport_socket_factory)), config_(config),
      stats_(generateUpstreamProxyProtocolStats(scope)) {}

Network::TransportSocketPtr UpstreamProxyProtocolSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr options,
    Upstream::HostDescriptionConstSharedPtr host) const {
  auto inner_socket = transport_socket_factory_->createTransportSocket(options, host);
  if (inner_socket == nullptr) {
    return nullptr;
  }
  return std::make_unique<UpstreamProxyProtocolSocket>(std::move(inner_socket), options, config_,
                                                       stats_);
}

void UpstreamProxyProtocolSocketFactory::hashKey(
    std::vector<uint8_t>& key, Network::TransportSocketOptionsConstSharedPtr options) const {
  PassthroughFactory::hashKey(key, options);
  // Proxy protocol options should only be included in the hash if the upstream
  // socket intends to use them.
  if (options) {
    const auto& proxy_protocol_options = options->proxyProtocolOptions();
    if (proxy_protocol_options.has_value()) {
      pushScalarToByteVector(
          StringUtil::CaseInsensitiveHash()(proxy_protocol_options.value().asStringForHash()), key);
    }
  }
}

absl::flat_hash_map<uint8_t, std::vector<unsigned char>>
UpstreamProxyProtocolSocket::buildCustomTLVs() {
  absl::flat_hash_map<uint8_t, std::vector<unsigned char>> custom_tlvs;

  const auto& upstream_info = callbacks_->connection().streamInfo().upstreamInfo();
  if (upstream_info == nullptr) {
    return custom_tlvs;
  }
  Upstream::HostDescriptionConstSharedPtr host = upstream_info->upstreamHost();
  if (host == nullptr) {
    return custom_tlvs;
  }
  auto metadata = host->metadata();
  if (metadata == nullptr) {
    return custom_tlvs;
  }

  const auto filter_it = metadata->typed_filter_metadata().find(
      Envoy::Config::MetadataFilters::get().ENVOY_TRANSPORT_SOCKETS_PROXY_PROTOCOL);
  if (filter_it == metadata->typed_filter_metadata().end()) {
    ENVOY_LOG(trace, "no custom TLVs found in upstream host metadata");
    return custom_tlvs;
  }

  envoy::extensions::transport_sockets::proxy_protocol::v3::CustomTlvMetadata custom_metadata;
  if (!filter_it->second.UnpackTo(&custom_metadata)) {
    ENVOY_LOG(warn, "failed to unpack custom TLVs from upstream host metadata");
    return custom_tlvs;
  }
  for (const auto& tlv : custom_metadata.entries()) {
    // prevent duplicate TLVs
    if (custom_tlvs.find(tlv.type()) != custom_tlvs.end()) {
      ENVOY_LOG(warn, "duplicate custom TLV type {} found in upstream host metadata", tlv.type());
      continue;
    }
    // prevent empty TLV values
    if (tlv.value().empty()) {
      ENVOY_LOG(warn, "empty custom TLV value found in upstream host metadata for type {}",
                tlv.type());
      continue;
    }
    custom_tlvs[tlv.type()] = std::vector<unsigned char>(tlv.value().begin(), tlv.value().end());
  }

  return custom_tlvs;
}

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
