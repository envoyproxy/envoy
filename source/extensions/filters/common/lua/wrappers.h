#pragma once

#include "envoy/buffer/buffer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/lua/lua.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

/**
 * A wrapper for a buffer.
 */
class BufferWrapper : public BaseLuaObject<BufferWrapper> {
public:
  BufferWrapper(Http::RequestOrResponseHeaderMap& headers, Buffer::Instance& data)
      : data_(data), headers_(headers) {}

  static ExportedFunctions exportedFunctions() {
    return {{"length", static_luaLength},
            {"getBytes", static_luaGetBytes},
            {"setBytes", static_luaSetBytes}};
  }

private:
  /**
   * @return int the size in bytes of the buffer.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaLength);

  /**
   * Get bytes out of a buffer for inspection in Lua.
   * @param 1 (int) starting index of bytes to extract.
   * @param 2 (int) length of bytes to extract.
   * @return string the extracted bytes. Throws an error if the index/length are out of range.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaGetBytes);

  /**
   * Set the wrapped data with the input string.
   * @param 1 (string) input string.
   * @return int the length of the input string.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaSetBytes);

  Buffer::Instance& data_;
  Http::RequestOrResponseHeaderMap& headers_;
};

class MetadataMapWrapper;

struct MetadataMapHelper {
  static void setValue(lua_State* state, const ProtobufWkt::Value& value);
  static void createTable(lua_State* state,
                          const Protobuf::Map<std::string, ProtobufWkt::Value>& fields);
  static ProtobufWkt::Value loadValue(lua_State* state);

private:
  static ProtobufWkt::Struct loadStruct(lua_State* state);
  static ProtobufWkt::ListValue loadList(lua_State* state, int length);
  static int tableLength(lua_State* state);
};

/**
 * Iterator over a metadata map.
 */
class MetadataMapIterator : public BaseLuaObject<MetadataMapIterator> {
public:
  MetadataMapIterator(MetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(MetadataMapIterator, luaPairsIterator);

private:
  MetadataMapWrapper& parent_;
  Protobuf::Map<std::string, ProtobufWkt::Value>::const_iterator current_;
};

/**
 * Lua wrapper for a metadata map.
 */
class MetadataMapWrapper : public BaseLuaObject<MetadataMapWrapper> {
public:
  MetadataMapWrapper(const ProtobufWkt::Struct& metadata) : metadata_{metadata} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet}, {"__pairs", static_luaPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter.
   * @return string value if found or nil.
   */
  DECLARE_LUA_FUNCTION(MetadataMapWrapper, luaGet);

  /**
   * Implementation of the __pairs metamethod so a metadata wrapper can be iterated over using
   * pairs().
   */
  DECLARE_LUA_FUNCTION(MetadataMapWrapper, luaPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  const ProtobufWkt::Struct metadata_;
  LuaDeathRef<MetadataMapIterator> iterator_;

  friend class MetadataMapIterator;
};

/**
 * Lua wrapper for Ssl::ConnectionInfo.
 */
class SslConnectionWrapper : public BaseLuaObject<SslConnectionWrapper> {
public:
  explicit SslConnectionWrapper(const Ssl::ConnectionInfo& info) : connection_info_{info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"peerCertificatePresented", static_luaPeerCertificatePresented},
            {"peerCertificateValidated", static_luaPeerCertificateValidated},
            {"uriSanLocalCertificate", static_luaUriSanLocalCertificate},
            {"sha256PeerCertificateDigest", static_luaSha256PeerCertificateDigest},
            {"serialNumberPeerCertificate", static_luaSerialNumberPeerCertificate},
            {"issuerPeerCertificate", static_luaIssuerPeerCertificate},
            {"subjectPeerCertificate", static_luaSubjectPeerCertificate},
            {"uriSanPeerCertificate", static_luaUriSanPeerCertificate},
            {"subjectLocalCertificate", static_luaSubjectLocalCertificate},
            {"dnsSansPeerCertificate", static_luaDnsSansPeerCertificate},
            {"dnsSansLocalCertificate", static_luaDnsSansLocalCertificate},
            {"oidsPeerCertificate", static_luaOidsPeerCertificate},
            {"oidsLocalCertificate", static_luaOidsLocalCertificate},
            {"validFromPeerCertificate", static_luaValidFromPeerCertificate},
            {"expirationPeerCertificate", static_luaExpirationPeerCertificate},
            {"sessionId", static_luaSessionId},
            {"ciphersuiteId", static_luaCiphersuiteId},
            {"ciphersuiteString", static_luaCiphersuiteString},
            {"urlEncodedPemEncodedPeerCertificate", static_luaUrlEncodedPemEncodedPeerCertificate},
            {"urlEncodedPemEncodedPeerCertificateChain",
             static_luaUrlEncodedPemEncodedPeerCertificateChain},
            {"tlsVersion", static_luaTlsVersion}};
  }

private:
  /**
   * Returns bool whether the peer certificate is presented.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaPeerCertificatePresented);

  /**
   * Returns bool whether the peer certificate is validated.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaPeerCertificateValidated);

  /**
   * Returns the URIs in the SAN field of the local certificate. Returns empty table if there is no
   * local certificate, or no SAN field, or no URI in SAN.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaUriSanLocalCertificate);

  /**
   * Returns the subject field of the local certificate in RFC 2253 format. Returns empty string if
   * there is no local certificate, or no subject.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaSubjectLocalCertificate);

  /**
   * Returns the SHA256 digest of the peer certificate. Returns empty string if there is no peer
   * certificate which can happen in TLS (non mTLS) connections.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaSha256PeerCertificateDigest);

  /**
   * Returns the serial number field of the peer certificate. Returns empty string if there is no
   * peer certificate, or no serial number.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaSerialNumberPeerCertificate);

  /**
   * Returns the issuer field of the peer certificate in RFC 2253 format. Returns empty string if
   * there is no peer certificate, or no issuer.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaIssuerPeerCertificate);

  /**
   * Returns the subject field of the peer certificate in RFC 2253 format. Returns empty string if
   * there is no peer certificate, or no subject.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaSubjectPeerCertificate);

  /**
   * Returns the URIs in the SAN field of the peer certificate. Returns empty table if there is no
   * peer certificate, or no SAN field, or no URI.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaUriSanPeerCertificate);

  /**
   * Return string the URL-encoded PEM-encoded representation of the peer certificate. Returns empty
   * string if there is no peer certificate or encoding fails.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaUrlEncodedPemEncodedPeerCertificate);

  /**
   * Returns the URL-encoded PEM-encoded representation of the full peer certificate chain including
   * the leaf certificate. Returns empty string if there is no peer certificate or encoding fails.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaUrlEncodedPemEncodedPeerCertificateChain);

  /**
   * Returns the DNS entries in the SAN field of the peer certificate. Returns an empty table if
   * there is no peer certificate, or no SAN field, or no DNS entries in SAN.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaDnsSansPeerCertificate);

  /**
   * Returns the DNS entries in the SAN field of the local certificate. Returns an empty table if
   * there is no local certificate, or no SAN field, or no DNS entries in SAN.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaDnsSansLocalCertificate);

  /**
   * Returns the OIDs (ASN.1 Object Identifiers) of the peer certificate. Returns an empty table if
   * there is no peer certificate or no OIDs.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaOidsPeerCertificate);

  /**
   * Returns the OIDs (ASN.1 Object Identifiers) of the peer certificate. Returns an empty table if
   * there is no peer certificate or no OIDs.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaOidsLocalCertificate);

  /**
   * Returns the timestamp-since-epoch (in seconds) that the peer certificate was issued and should
   * be considered valid from. Returns empty string if there is no peer certificate.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaValidFromPeerCertificate);

  /**
   * Returns the timestamp-since-epoch (in seconds) that the peer certificate expires and should not
   * be considered valid after. Returns empty string if there is no peer certificate.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaExpirationPeerCertificate);

  /**
   * Returns the hex-encoded TLS session ID as defined in RFC 5246.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaSessionId);

  /**
   * Returns the standard ID for the ciphers used in the established TLS connection. Returns 0xffff
   * if there is no current negotiated ciphersuite.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaCiphersuiteId);

  /**
   * Returns the OpenSSL name for the set of ciphers used in the established TLS connection. Returns
   * empty string if there is no current negotiated ciphersuite.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaCiphersuiteString);

  /**
   * Returns the TLS version (e.g. TLSv1.2, TLSv1.3) used in the established TLS connection. Returns
   * string if secured and nil if not.
   */
  DECLARE_LUA_FUNCTION(SslConnectionWrapper, luaTlsVersion);

  // TODO(dio): Add luaX509Extension if required, since currently it is used out of tree.

  const Ssl::ConnectionInfo& connection_info_;
};

/**
 * Lua wrapper for Network::Connection.
 */
class ConnectionWrapper : public BaseLuaObject<ConnectionWrapper> {
public:
  ConnectionWrapper(const Network::Connection* connection) : connection_{connection} {}

  // TODO(dio): Remove this in favor of StreamInfo::downstreamSslConnection wrapper since ssl() in
  // envoy/network/connection.h is subject to removal.
  static ExportedFunctions exportedFunctions() { return {{"ssl", static_luaSsl}}; }

private:
  /**
   * Get the Ssl::Connection wrapper
   * @return object if secured and nil if not.
   */
  DECLARE_LUA_FUNCTION(ConnectionWrapper, luaSsl);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override { ssl_connection_wrapper_.reset(); }

  const Network::Connection* connection_;
  LuaDeathRef<SslConnectionWrapper> ssl_connection_wrapper_;
};

} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
