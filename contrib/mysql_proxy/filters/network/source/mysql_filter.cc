#include "contrib/mysql_proxy/filters/network/source/mysql_filter.h"

#include <algorithm>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_decoder_impl.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"
#include "openssl/evp.h"
#include "openssl/rsa.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

MySQLFilterConfig::MySQLFilterConfig(const std::string& stat_prefix, Stats::Scope& scope,
                                     SSLMode downstream_ssl)
    : scope_(scope), stats_(generateStats(stat_prefix, scope)), downstream_ssl_(downstream_ssl) {}

MySQLFilter::MySQLFilter(MySQLFilterConfigSharedPtr config) : config_(std::move(config)) {}

void MySQLFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

void MySQLFilter::initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) {
  write_callbacks_ = &callbacks;
}

Network::FilterStatus MySQLFilter::onData(Buffer::Instance& data, bool) {
  Network::FilterStatus status = Network::FilterStatus::Continue;
  uint64_t remaining = read_buffer_.length();

  // Safety measure just to make sure that if we have a decoding error we keep going and lose stats.
  // This can be removed once we are more confident of this code.
  if (!sniffing_) {
    return status;
  }

  read_buffer_.add(data);
  status = doDecode(read_buffer_, true);

  if (status == Network::FilterStatus::StopIteration) {
    data.drain(data.length());
    return status;
  }

  // RSA mediation: intercept client cleartext password.
  if (rsa_auth_state_ == RsaAuthState::WaitingClientPassword && cleartext_password_) {
    data.drain(data.length());

    uint8_t inject_seq = getSession().getExpectedSeq(false) - 1;
    ENVOY_CONN_LOG(trace,
                   "mysql_proxy: intercepted client password, sending request-public-key (seq={})",
                   read_callbacks_->connection(), inject_seq);

    Buffer::OwnedImpl buf;
    BufferHelper::addUint24(buf, 1);
    BufferHelper::addUint8(buf, inject_seq);
    BufferHelper::addUint8(buf, MYSQL_REQUEST_PUBLIC_KEY);
    read_callbacks_->injectReadDataToFilterChain(buf, false);

    rsa_auth_state_ = RsaAuthState::WaitingServerKey;
    return Network::FilterStatus::StopIteration;
  }

  if (config_->terminateSsl()) {
    doRewrite(data, remaining, true);
  }

  return status;
}

Network::FilterStatus MySQLFilter::onWrite(Buffer::Instance& data, bool) {
  Network::FilterStatus status = Network::FilterStatus::Continue;

  // Safety measure just to make sure that if we have a decoding error we keep going and lose stats.
  // This can be removed once we are more confident of this code.
  if (!sniffing_) {
    return status;
  }

  // RSA mediation: intercept server's public key response.
  if (rsa_auth_state_ == RsaAuthState::WaitingServerKey) {
    write_buffer_.add(data);
    data.drain(data.length());

    // Check if we have a complete packet.
    uint32_t len = 0;
    uint8_t seq = 0;
    if (BufferHelper::peekHdr(write_buffer_, len, seq) != DecodeStatus::Success ||
        sizeof(uint32_t) + len > write_buffer_.length()) {
      ENVOY_CONN_LOG(trace,
                     "mysql_proxy: waiting for complete public key packet ({} bytes buffered)",
                     read_callbacks_->connection(), write_buffer_.length());
      return Network::FilterStatus::StopIteration;
    }

    ENVOY_CONN_LOG(trace, "mysql_proxy: received server public key packet (seq={}, len={})",
                   read_callbacks_->connection(), seq, len);

    // Full packet available. Parse it: [hdr][0x01 marker][PEM key bytes].
    BufferHelper::consumeHdr(write_buffer_);
    uint8_t marker;
    BufferHelper::readUint8(write_buffer_, marker);
    if (marker != MYSQL_RESP_MORE) {
      ENVOY_CONN_LOG(error, "mysql_proxy: unexpected marker 0x{:02x} in public key response",
                     read_callbacks_->connection(), marker);
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::StopIteration;
    }

    std::string pem_key;
    BufferHelper::readStringBySize(write_buffer_, len - 1, pem_key);
    write_buffer_.drain(write_buffer_.length());

    ENVOY_CONN_LOG(trace, "mysql_proxy: extracted PEM key ({} bytes), encrypting password",
                   read_callbacks_->connection(), pem_key.size());

    sendEncryptedPassword(pem_key, seq);
    getSession().adjustSeqOffset(-2);
    rsa_auth_state_ = RsaAuthState::WaitingServerResult;

    ENVOY_CONN_LOG(trace, "mysql_proxy: RSA encrypted password sent, waiting for server result",
                   read_callbacks_->connection());

    // Nothing to forward to client; data was already drained.
    return Network::FilterStatus::Continue;
  }

  uint64_t remaining = write_buffer_.length();

  write_buffer_.add(data);
  status = doDecode(write_buffer_, false);

  if (status == Network::FilterStatus::StopIteration) {
    data.drain(data.length());
    return status;
  }

  if (config_->terminateSsl()) {
    doRewrite(data, remaining, false);
  }

  return status;
}

bool MySQLFilter::onSSLRequest() {
  if (!config_->terminateSsl()) {
    return true;
  }

  if (!read_callbacks_->connection().startSecureTransport()) {
    ENVOY_CONN_LOG(info, "mysql_proxy: cannot enable secure transport. Check configuration.",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  } else {
    ENVOY_CONN_LOG(trace, "mysql_proxy: enabled SSL termination.", read_callbacks_->connection());
  }

  return false;
}

Network::FilterStatus MySQLFilter::doDecode(Buffer::Instance& buffer, bool is_upstream) {
  // Clear dynamic metadata.
  envoy::config::core::v3::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  auto& metadata =
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy];
  metadata.mutable_fields()->clear();

  if (!decoder_) {
    decoder_ = createDecoder(*this);
  }

  try {
    switch (decoder_->onData(buffer, is_upstream)) {
    case Decoder::Result::ReadyForNext:
      return Network::FilterStatus::Continue;
    case Decoder::Result::Stopped:
      return Network::FilterStatus::StopIteration;
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG(info, "mysql_proxy: decoding error: {}", e.what());
    config_->stats_.decoder_errors_.inc();
    sniffing_ = false;
    read_buffer_.drain(read_buffer_.length());
    write_buffer_.drain(write_buffer_.length());
  }

  return Network::FilterStatus::Continue;
}

DecoderPtr MySQLFilter::createDecoder(DecoderCallbacks& callbacks) {
  return std::make_unique<DecoderImpl>(callbacks);
}

void MySQLFilter::rewritePacketHeader(Buffer::Instance& data, uint8_t seq, uint32_t len) {
  BufferHelper::consumeHdr(data);
  BufferHelper::addUint24(data, len);
  BufferHelper::addUint8(data, seq);
}

void MySQLFilter::stripSslCapability(Buffer::Instance& data) {
  uint32_t client_cap = 0;
  BufferHelper::readUint32(data, client_cap);
  BufferHelper::addUint32(data, client_cap & ~CLIENT_SSL);
}

void MySQLFilter::doRewrite(Buffer::Instance& data, uint64_t remaining, bool is_upstream) {
  MySQLSession::State state = getSession().getState();
  auto& payload_metadata_list = decoder_->getPayloadMetadataList();
  const uint64_t original_data_size = data.length();
  uint64_t max_data_size = original_data_size;

  for (size_t i = 0; i < payload_metadata_list.size(); ++i) {
    uint8_t seq = payload_metadata_list[i].seq;
    uint32_t len = payload_metadata_list[i].len;

    if (i == 0 && remaining > 0) {
      // First packet spans old internal buffer and new data. The header and first
      // (remaining - 4) payload bytes are in the internal buffer, not in data.
      ASSERT(remaining >= 4, "partial header should not appear in payload metadata");
      len -= remaining - 4;
    } else {
      rewritePacketHeader(data, seq, len);
      max_data_size -= 4;

      if (is_upstream && (state == MySQLSession::State::ChallengeResp41 ||
                          state == MySQLSession::State::ChallengeResp320)) {
        stripSslCapability(data);
        len -= 4;
      }
    }

    uint64_t copy_size = std::min(static_cast<uint64_t>(len), max_data_size);
    std::string payload;
    payload.reserve(copy_size);
    BufferHelper::readStringBySize(data, copy_size, payload);
    BufferHelper::addBytes(data, payload.c_str(), payload.size());
    max_data_size -= copy_size;
  }

  ASSERT(data.length() == original_data_size, "doRewrite must not change overall buffer size");
}

void MySQLFilter::onProtocolError() { config_->stats_.protocol_errors_.inc(); }

void MySQLFilter::onServerGreeting(ServerGreeting& greeting) {
  config_->stats_.login_attempts_.inc();
  ENVOY_CONN_LOG(trace, "mysql_proxy: server greeting: version={}, auth_plugin={}, scramble_len={}",
                 read_callbacks_->connection(), greeting.getVersion(), greeting.getAuthPluginName(),
                 greeting.getAuthPluginData().size());
  if (config_->terminateSsl()) {
    server_scramble_ = greeting.getAuthPluginData();
    // The MySQL greeting protocol may include a trailing null filler byte in
    // auth_plugin_data, making it 21 bytes. The actual nonce used by
    // caching_sha2_password is always 20 bytes. Truncate to avoid corrupting
    // the XOR for passwords longer than 20 characters.
    if (server_scramble_.size() > NATIVE_PSSWORD_HASH_LENGTH) {
      server_scramble_.resize(NATIVE_PSSWORD_HASH_LENGTH);
    }
    auth_plugin_name_ = greeting.getAuthPluginName();
    ENVOY_CONN_LOG(trace,
                   "mysql_proxy: captured scramble ({} bytes) for SSL termination, plugin={}",
                   read_callbacks_->connection(), server_scramble_.size(), auth_plugin_name_);
  }
}

void MySQLFilter::onClientLogin(ClientLogin& client_login, MySQLSession::State state) {
  ENVOY_CONN_LOG(trace, "mysql_proxy: client login: ssl_request={}, state={}, user={}",
                 read_callbacks_->connection(), client_login.isSSLRequest(),
                 static_cast<int>(state), client_login.getUsername());
  if (client_login.isSSLRequest()) {
    config_->stats_.upgraded_to_ssl_.inc();
  }

  if (state == MySQLSession::State::ChallengeResp41 ||
      state == MySQLSession::State::ChallengeResp320) {
    // REQUIRE mode: reject clients that did not initiate SSL.
    using MySQLProto = envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy;
    if (config_->downstream_ssl_ == MySQLProto::REQUIRE && getSession().getSeqOffset() == 0) {
      ENVOY_CONN_LOG(info,
                     "mysql_proxy: downstream_ssl=REQUIRE but client did not initiate SSL, closing",
                     read_callbacks_->connection());
      read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    }
  }
}

void MySQLFilter::onClientLoginResponse(ClientLoginResponse& client_login_resp) {
  ENVOY_CONN_LOG(trace, "mysql_proxy: server login response: resp_code=0x{:02x}",
                 read_callbacks_->connection(), client_login_resp.getRespCode());
  if (client_login_resp.getRespCode() == MYSQL_RESP_AUTH_SWITCH) {
    config_->stats_.auth_switch_request_.inc();
  } else if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
    config_->stats_.login_failures_.inc();
  } else if (config_->terminateSsl() && getSession().getSeqOffset() != 0 &&
             client_login_resp.getRespCode() == MYSQL_RESP_MORE) {
    auto* auth_more = dynamic_cast<AuthMoreMessage*>(&client_login_resp);
    if (auth_more && !auth_more->getAuthMoreData().empty()) {
      ENVOY_CONN_LOG(trace, "mysql_proxy: AuthMoreData[0]=0x{:02x}, plugin={}",
                     read_callbacks_->connection(), auth_more->getAuthMoreData()[0],
                     auth_plugin_name_);
      if (auth_more->getAuthMoreData()[0] == MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED &&
          auth_plugin_name_ == "caching_sha2_password") {
        rsa_auth_state_ = RsaAuthState::WaitingClientPassword;
        ENVOY_CONN_LOG(trace, "mysql_proxy: full auth required, entering RSA mediation",
                       read_callbacks_->connection());
      } else if (auth_more->getAuthMoreData()[0] == MYSQL_CACHINGSHA2_FAST_AUTH_SUCCESS) {
        ENVOY_CONN_LOG(trace, "mysql_proxy: fast auth success (cache hit), no RSA needed",
                       read_callbacks_->connection());
      }
    }
  }
}

void MySQLFilter::onMoreClientLoginResponse(ClientLoginResponse& client_login_resp) {
  ENVOY_CONN_LOG(trace, "mysql_proxy: more login response: resp_code=0x{:02x}, rsa_state={}",
                 read_callbacks_->connection(), client_login_resp.getRespCode(),
                 static_cast<int>(rsa_auth_state_));
  if (client_login_resp.getRespCode() == MYSQL_RESP_ERR) {
    config_->stats_.login_failures_.inc();
  }
  if (rsa_auth_state_ == RsaAuthState::WaitingServerResult) {
    ENVOY_CONN_LOG(trace, "mysql_proxy: RSA mediation complete, result=0x{:02x}",
                   read_callbacks_->connection(), client_login_resp.getRespCode());
    rsa_auth_state_ = RsaAuthState::Inactive;
  }
}

void MySQLFilter::onAuthSwitchMoreClientData(std::unique_ptr<SecureBytes> data) {
  ENVOY_CONN_LOG(trace, "mysql_proxy: client auth data received, len={}, rsa_state={}",
                 read_callbacks_->connection(), data ? data->size() : 0,
                 static_cast<int>(rsa_auth_state_));
  if (rsa_auth_state_ == RsaAuthState::WaitingClientPassword && data) {
    // Password arrives in SecureBytes (guarded memory, zeroed on free).
    // The decoder already read it via readSecureBytes which zeroed the source buffer.
    cleartext_password_ = std::move(data);
    ENVOY_CONN_LOG(trace, "mysql_proxy: captured cleartext password ({} bytes) in secure memory",
                   read_callbacks_->connection(), cleartext_password_->size());
  }
}

void MySQLFilter::sendEncryptedPassword(const std::string& pem_key, uint8_t last_server_seq) {
  if (!cleartext_password_) {
    ENVOY_CONN_LOG(error, "mysql_proxy: no cleartext password captured",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  // XOR password with scramble in secure memory. The cleartext_password_ includes the
  // trailing null sent by the client (password\0 format for caching_sha2_password).
  SecureBytes xored(cleartext_password_->size());
  for (size_t i = 0; i < cleartext_password_->size(); i++) {
    xored[i] = (*cleartext_password_)[i] ^ server_scramble_[i % server_scramble_.size()];
  }

  // Import the server's public key.
  auto pkey = Envoy::Common::Crypto::UtilitySingleton::get().importPublicKeyPEM(pem_key);
  if (!pkey || !pkey->getEVP_PKEY()) {
    ENVOY_CONN_LOG(error, "mysql_proxy: failed to import server public key",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  // RSA-encrypt with OAEP/SHA-1 padding (MySQL requirement).
  bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(pkey->getEVP_PKEY(), nullptr));
  if (!ctx || EVP_PKEY_encrypt_init(ctx.get()) <= 0 ||
      EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0 ||
      EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha1()) <= 0) {
    ENVOY_CONN_LOG(error, "mysql_proxy: failed to initialize RSA encryption context",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  size_t out_len = 0;
  if (EVP_PKEY_encrypt(ctx.get(), nullptr, &out_len, xored.data(), xored.size()) <= 0) {
    ENVOY_CONN_LOG(error, "mysql_proxy: failed to determine RSA ciphertext length",
                   read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    cleartext_password_.reset();
    return;
  }

  std::vector<uint8_t> encrypted(out_len);
  if (EVP_PKEY_encrypt(ctx.get(), encrypted.data(), &out_len, xored.data(), xored.size()) <= 0) {
    ENVOY_CONN_LOG(error, "mysql_proxy: RSA encryption failed", read_callbacks_->connection());
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    cleartext_password_.reset();
    return;
  }
  encrypted.resize(out_len);

  // Build the encrypted password packet: [3-byte len][seq][encrypted_data].
  uint8_t enc_seq = static_cast<uint8_t>(last_server_seq + 1);
  Buffer::OwnedImpl buf;
  BufferHelper::addUint24(buf, encrypted.size());
  BufferHelper::addUint8(buf, enc_seq);
  BufferHelper::addVector(buf, encrypted);

  ENVOY_CONN_LOG(trace,
                 "mysql_proxy: injecting RSA-encrypted password (seq={}, {} bytes ciphertext)",
                 read_callbacks_->connection(), enc_seq, encrypted.size());

  // Inject toward server (bypasses our filter).
  read_callbacks_->injectReadDataToFilterChain(buf, false);

  // Securely destroy the cleartext password (OPENSSL_cleanse zeroes before freeing).
  cleartext_password_.reset();
}

void MySQLFilter::onCommand(Command& command) {
  if (!command.isQuery()) {
    return;
  }

  // Parse a given query
  envoy::config::core::v3::Metadata& dynamic_metadata =
      read_callbacks_->connection().streamInfo().dynamicMetadata();
  Protobuf::Struct metadata(
      (*dynamic_metadata.mutable_filter_metadata())[NetworkFilterNames::get().MySQLProxy]);

  auto result = Common::SQLUtils::SQLUtils::setMetadata(command.getData(),
                                                        decoder_->getAttributes(), metadata);

  ENVOY_CONN_LOG(trace, "mysql_proxy: query processed {}, result {}, cmd type {}",
                 read_callbacks_->connection(), command.getData(), result,
                 static_cast<int>(command.getCmd()));

  if (!result) {
    config_->stats_.queries_parse_error_.inc();
    return;
  }
  config_->stats_.queries_parsed_.inc();

  read_callbacks_->connection().streamInfo().setDynamicMetadata(
      NetworkFilterNames::get().MySQLProxy, metadata);
}

Network::FilterStatus MySQLFilter::onNewConnection() {
  config_->stats_.sessions_.inc();
  return Network::FilterStatus::Continue;
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
