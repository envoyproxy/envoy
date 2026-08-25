#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/crypto/utility.h"
#include "source/extensions/filters/common/lua/lua.h"
#include "source/extensions/filters/common/lua/wrappers.h"

#include "openssl/evp.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

class HeaderMapWrapperBase;

/**
 * Iterator over a header map.
 */
class HeaderMapIterator : public Filters::Common::Lua::BaseLuaObject<HeaderMapIterator> {
public:
  HeaderMapIterator(HeaderMapWrapperBase& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(HeaderMapIterator, luaPairsIterator);

private:
  // The base, not either concrete wrapper: iteration reads headers and clears the parent's
  // iterator slot, both of which live in the base, so one iterator type serves the read-write
  // and read-only wrappers alike.
  HeaderMapWrapperBase& parent_;
  std::vector<const Http::HeaderEntry*> entries_;
  uint64_t current_{};
};

/**
 * Shared read-only half of the header map wrappers.
 *
 * Not a BaseLuaObject itself: each concrete wrapper derives from BaseLuaObject with its own type,
 * because a Lua metatable is registered per C++ type (see registerType() and the typeid() key in
 * DECLARE_LUA_FUNCTION). That is exactly what makes ReadOnlyHeaderMapWrapper read-only -- the
 * mutating methods are absent from its metatable rather than present and refusing.
 */
class HeaderMapWrapperBase {
public:
  HeaderMapWrapperBase(Http::HeaderMap& headers) : headers_(headers) {}
  virtual ~HeaderMapWrapperBase() = default;

protected:
  /**
   * Get a header value from the map.
   * @param 1 (string): header name.
   * @return string value if found or nil.
   */
  int luaGet(lua_State* state);

  /**
   * Get a header value from the map.
   * @param 1 (string): header name.
   * @param 2 (int): index of the value for the given header which needs to be retrieved.
   * @return string value if found or nil.
   */
  int luaGetAtIndex(lua_State* state);

  /**
   * Get the header value size from the map.
   * @param 1 (string): header name.
   * @return int value size if found or 0.
   */
  int luaGetNumValues(lua_State* state);

  /**
   * Implementation of the __pairs metamethod so a headers wrapper can be iterated over using
   * pairs().
   */
  int luaPairs(lua_State* state);

  Http::HeaderMap& headers_;
  Filters::Common::Lua::LuaDeathRef<HeaderMapIterator> iterator_;

  friend class HeaderMapIterator;
};

/**
 * Lua wrapper for a mutable header map. The mutating methods call a check function to see if
 * modification is allowed at this point in the stream; when it is not, they raise.
 *
 * For a map that is never writable, use ReadOnlyHeaderMapWrapper below rather than passing a
 * callback that always returns false: a caller should not be told "not right now" about something
 * that is never possible.
 */
class HeaderMapWrapper : public HeaderMapWrapperBase,
                         public Filters::Common::Lua::BaseLuaObject<HeaderMapWrapper> {
public:
  using CheckModifiableCb = std::function<bool()>;

  HeaderMapWrapper(Http::HeaderMap& headers, CheckModifiableCb cb)
      : HeaderMapWrapperBase(headers), cb_(cb) {}

  static ExportedFunctions exportedFunctions() {
    return {{"add", static_luaAdd},
            {"get", static_luaGet},
            {"getAtIndex", static_luaGetAtIndex},
            {"getNumValues", static_luaGetNumValues},
            {"remove", static_luaRemove},
            {"replace", static_luaReplace},
            {"setHttp1ReasonPhrase", static_luaSetHttp1ReasonPhrase},
            {"__pairs", static_luaPairs}};
  }

private:
  /**
   * Add a header to the map.
   * @param 1 (string): header name.
   * @param 2 (string): header value.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaAdd);

  /**
   * Remove a header from the map.
   * @param 1 (string): header name.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaRemove);

  /**
   * Replace a header in the map. If the header does not exist, it will be added.
   * @param 1 (string): header name.
   * @param 2 (string): header value.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaReplace);

  /**
   * Set a HTTP1 reason phrase
   * @param 1 (string): reason phrase
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HeaderMapWrapper, luaSetHttp1ReasonPhrase);

  // Read methods are implemented in HeaderMapWrapperBase; these only generate the thunks that
  // bind them to this type's metatable.
  FORWARD_LUA_FUNCTION(HeaderMapWrapper, luaGet)
  FORWARD_LUA_FUNCTION(HeaderMapWrapper, luaGetAtIndex)
  FORWARD_LUA_FUNCTION(HeaderMapWrapper, luaGetNumValues)
  FORWARD_LUA_FUNCTION(HeaderMapWrapper, luaPairs)

  void checkModifiable(lua_State* state);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  CheckModifiableCb cb_;
};

/**
 * Lua wrapper for a header map that can only be read.
 *
 * The mutating methods are not in this type's metatable at all, so `h:replace(...)` fails as a
 * call of a nil value rather than as a policy error at call time. That distinction is the point:
 * a wrapper that carries `add`/`remove`/`replace` and refuses them still reports them as present
 * to anything that inspects it, and only reveals the truth by failing a request in production.
 *
 * Used for the downstream request headers exposed on the response path, which cannot be modified
 * usefully -- they have already been sent upstream.
 */
class ReadOnlyHeaderMapWrapper
    : public HeaderMapWrapperBase,
      public Filters::Common::Lua::BaseLuaObject<ReadOnlyHeaderMapWrapper> {
public:
  ReadOnlyHeaderMapWrapper(Http::HeaderMap& headers) : HeaderMapWrapperBase(headers) {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet},
            {"getAtIndex", static_luaGetAtIndex},
            {"getNumValues", static_luaGetNumValues},
            {"__pairs", static_luaPairs}};
  }

private:
  FORWARD_LUA_FUNCTION(ReadOnlyHeaderMapWrapper, luaGet)
  FORWARD_LUA_FUNCTION(ReadOnlyHeaderMapWrapper, luaGetAtIndex)
  FORWARD_LUA_FUNCTION(ReadOnlyHeaderMapWrapper, luaGetNumValues)
  FORWARD_LUA_FUNCTION(ReadOnlyHeaderMapWrapper, luaPairs)

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }
};

class DynamicMetadataMapWrapper;
class StreamInfoWrapper;
class ConnectionDynamicMetadataMapWrapper;
class ConnectionStreamInfoWrapper;

/**
 * Iterator over a dynamic metadata map.
 */
class DynamicMetadataMapIterator
    : public Filters::Common::Lua::BaseLuaObject<DynamicMetadataMapIterator> {
public:
  DynamicMetadataMapIterator(DynamicMetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(DynamicMetadataMapIterator, luaPairsIterator);

private:
  DynamicMetadataMapWrapper& parent_;
  Protobuf::Map<std::string, Protobuf::Struct>::const_iterator current_;
};

/**
 * Iterator over a network filter dynamic metadata map.
 */
class ConnectionDynamicMetadataMapIterator
    : public Filters::Common::Lua::BaseLuaObject<ConnectionDynamicMetadataMapIterator> {
public:
  ConnectionDynamicMetadataMapIterator(ConnectionDynamicMetadataMapWrapper& parent);

  static ExportedFunctions exportedFunctions() { return {}; }

  DECLARE_LUA_CLOSURE(ConnectionDynamicMetadataMapIterator,
                      luaConnectionDynamicMetadataPairsIterator);

private:
  ConnectionDynamicMetadataMapWrapper& parent_;
  Protobuf::Map<std::string, Protobuf::Struct>::const_iterator current_;
};

/**
 * Lua wrapper for a dynamic metadata.
 */
class DynamicMetadataMapWrapper
    : public Filters::Common::Lua::BaseLuaObject<DynamicMetadataMapWrapper> {
public:
  DynamicMetadataMapWrapper(StreamInfoWrapper& parent) : parent_{parent} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet}, {"set", static_luaSet}, {"__pairs", static_luaPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @return value if found or nil.
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaGet);

  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @param 2 (string or table): key.
   * @param 3 (string or table): value.
   * @return nil.
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaSet);

  /**
   * Implementation of the __pairs metamethod so a dynamic metadata wrapper can be iterated over
   * using pairs().
   */
  DECLARE_LUA_FUNCTION(DynamicMetadataMapWrapper, luaPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  // To get reference to parent's (StreamInfoWrapper) stream info member.
  StreamInfo::StreamInfo& streamInfo();

  StreamInfoWrapper& parent_;
  Filters::Common::Lua::LuaDeathRef<DynamicMetadataMapIterator> iterator_;

  friend class DynamicMetadataMapIterator;
};

/**
 * Lua wrapper for a network filter dynamic metadata.
 */
class ConnectionDynamicMetadataMapWrapper
    : public Filters::Common::Lua::BaseLuaObject<ConnectionDynamicMetadataMapWrapper> {
public:
  ConnectionDynamicMetadataMapWrapper(ConnectionStreamInfoWrapper& parent) : parent_{parent} {}

  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaConnectionDynamicMetadataGet},
            {"__pairs", static_luaConnectionDynamicMetadataPairs}};
  }

private:
  /**
   * Get a metadata value from the map.
   * @param 1 (string): filter name.
   * @return value if found or nil.
   */
  DECLARE_LUA_FUNCTION(ConnectionDynamicMetadataMapWrapper, luaConnectionDynamicMetadataGet);

  /**
   * Implementation of the __pairs meta method so a dynamic metadata wrapper can be iterated over
   * using pairs().
   */
  DECLARE_LUA_FUNCTION(ConnectionDynamicMetadataMapWrapper, luaConnectionDynamicMetadataPairs);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    // Iterators do not survive yields.
    iterator_.reset();
  }

  // To get reference to parent's (StreamInfoWrapper) stream info member.
  const StreamInfo::StreamInfo& streamInfo();

  ConnectionStreamInfoWrapper& parent_;
  Filters::Common::Lua::LuaDeathRef<ConnectionDynamicMetadataMapIterator> iterator_;

  friend class ConnectionDynamicMetadataMapIterator;
};

/**
 * Lua wrapper for accessing filter state objects.
 */
class FilterStateWrapper : public Filters::Common::Lua::BaseLuaObject<FilterStateWrapper> {
public:
  FilterStateWrapper(StreamInfoWrapper& parent) : parent_(parent) {}
  static ExportedFunctions exportedFunctions() {
    return {{"get", static_luaGet}, {"set", static_luaSet}};
  }

private:
  /**
   * Get a filter state object by name, with an optional field name.
   * @param 1 (string): object name.
   * @param 2 (string, optional): field name for objects that support field access.
   * @return filter state value as string, or nil if not found.
   */
  DECLARE_LUA_FUNCTION(FilterStateWrapper, luaGet);

  /**
   * Set a filter state object by name using a registered factory.
   * @param 1 (string): object key (the name under which the object is stored).
   * @param 2 (string): factory key (the registered ObjectFactory name).
   * @param 3 (string): bytes payload to pass to the factory's createFromBytes.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(FilterStateWrapper, luaSet);

  StreamInfo::StreamInfo& streamInfo();

  StreamInfoWrapper& parent_;
};

/**
 * Lua wrapper for a stream info.
 */
class StreamInfoWrapper : public Filters::Common::Lua::BaseLuaObject<StreamInfoWrapper> {
public:
  StreamInfoWrapper(StreamInfo::StreamInfo& stream_info) : stream_info_{stream_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"protocol", static_luaProtocol},
            {"dynamicMetadata", static_luaDynamicMetadata},
            {"dynamicTypedMetadata", static_luaDynamicTypedMetadata},
            {"filterState", static_luaFilterState},
            {"downstreamDirectLocalAddress", static_luaDownstreamDirectLocalAddress},
            {"downstreamLocalAddress", static_luaDownstreamLocalAddress},
            {"downstreamDirectRemoteAddress", static_luaDownstreamDirectRemoteAddress},
            {"downstreamRemoteAddress", static_luaDownstreamRemoteAddress},
            {"downstreamSslConnection", static_luaDownstreamSslConnection},
            {"requestedServerName", static_luaRequestedServerName},
            {"routeName", static_luaRouteName},
            {"virtualClusterName", static_luaVirtualClusterName},
            {"drainConnectionUponCompletion", static_luaDrainConnectionUponCompletion}};
  }

private:
  /**
   * Get current protocol being used.
   * @return string representation of Http::Protocol.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaProtocol);

  /**
   * Get reference to stream info dynamic metadata object.
   * @return DynamicMetadataMapWrapper representation of StreamInfo dynamic metadata.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDynamicMetadata);

  /**
   * Get reference to stream info typed metadata object.
   * @return typed metadata wrapped as a Lua table.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDynamicTypedMetadata);

  /**
   * Get reference to stream info filter state objects.
   * @return filter state objects wrapped as a Lua table with string keys and serialized values.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaFilterState);

  /**
   * Get reference to stream info downstreamSslConnection.
   * @return SslConnectionWrapper representation of StreamInfo downstream SSL connection.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamSslConnection);

  /**
   * Get current downstream local address
   * @return string representation of downstream local address.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamLocalAddress);

  /**
   * Get current direct downstream local address
   * @return string representation of downstream directly connected local address.
   * This is equivalent to the local address of the physical connection.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamDirectLocalAddress);

  /**
   * Get current direct downstream remote address
   * @return string representation of downstream directly connected address.
   * This is equivalent to the address of the physical connection.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamDirectRemoteAddress);

  /**
   * Get current downstream remote address
   * @return string representation of downstream remote address.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDownstreamRemoteAddress);

  /**
   * Get requested server name
   * @return requested server name (e.g. SNI in TLS), if any.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaRequestedServerName);

  /**
   * Get the name of the route matched by the filter chain
   * @return matched route name or an empty string if no route was matched
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaRouteName);

  /**
   * Get the name of the virtual cluster that gets matched (if any)
   * @return matched virtual cluster or an empty string if no virtual cluster was matched
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaVirtualClusterName);

  /**
   * Drains the connection upon completion of this stream.
   * For HTTP/1.1 this will add "Connection: close" header.
   * For HTTP/2 and HTTP/3 this will trigger sending a GOAWAY frame.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(StreamInfoWrapper, luaDrainConnectionUponCompletion);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override {
    dynamic_metadata_wrapper_.reset();
    filter_state_wrapper_.reset();
    downstream_ssl_connection_.reset();
  }

  StreamInfo::StreamInfo& stream_info_;
  Filters::Common::Lua::LuaDeathRef<DynamicMetadataMapWrapper> dynamic_metadata_wrapper_;
  Filters::Common::Lua::LuaDeathRef<FilterStateWrapper> filter_state_wrapper_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::SslConnectionWrapper>
      downstream_ssl_connection_;

  friend class DynamicMetadataMapWrapper;
  friend class FilterStateWrapper;
};

/**
 * Lua wrapper for a network connection's stream info.
 */
class ConnectionStreamInfoWrapper
    : public Filters::Common::Lua::BaseLuaObject<ConnectionStreamInfoWrapper> {
public:
  ConnectionStreamInfoWrapper(const StreamInfo::StreamInfo& connection_stream_info)
      : connection_stream_info_{connection_stream_info} {}
  static ExportedFunctions exportedFunctions() {
    return {{"dynamicMetadata", static_luaConnectionDynamicMetadata},
            {"dynamicTypedMetadata", static_luaConnectionDynamicTypedMetadata}};
  }

private:
  /**
   * Get reference to stream info dynamic metadata object.
   * @return ConnectionDynamicMetadataMapWrapper representation of StreamInfo dynamic metadata.
   */
  DECLARE_LUA_FUNCTION(ConnectionStreamInfoWrapper, luaConnectionDynamicMetadata);

  /**
   * Get reference to stream info typed metadata object.
   * @return typed metadata wrapped as a Lua table.
   */
  DECLARE_LUA_FUNCTION(ConnectionStreamInfoWrapper, luaConnectionDynamicTypedMetadata);

  // Envoy::Lua::BaseLuaObject
  void onMarkDead() override { connection_dynamic_metadata_wrapper_.reset(); }

  const StreamInfo::StreamInfo& connection_stream_info_;
  Filters::Common::Lua::LuaDeathRef<ConnectionDynamicMetadataMapWrapper>
      connection_dynamic_metadata_wrapper_;

  friend class ConnectionDynamicMetadataMapWrapper;
};

/**
 * Lua wrapper for key for accessing the imported public keys.
 */
class PublicKeyWrapper : public Filters::Common::Lua::BaseLuaObject<PublicKeyWrapper> {
public:
  explicit PublicKeyWrapper(absl::string_view key) : public_key_(key) {}
  static ExportedFunctions exportedFunctions() { return {{"get", static_luaGet}}; }

private:
  /**
   * Get public key value.
   * @return public key value or nil if key is empty.
   */
  DECLARE_LUA_FUNCTION(PublicKeyWrapper, luaGet);

  const std::string public_key_;
};

class Timestamp {
public:
  enum Resolution { Millisecond, Microsecond, Undefined };
};

class VirtualHostWrapper : public Filters::Common::Lua::BaseLuaObject<VirtualHostWrapper> {
public:
  VirtualHostWrapper(const StreamInfo::StreamInfo& stream_info,
                     const absl::string_view filter_config_name)
      : stream_info_{stream_info}, filter_config_name_{filter_config_name} {}

  static ExportedFunctions exportedFunctions() { return {{"metadata", static_luaMetadata}}; }

private:
  /**
   * @return a handle to the metadata.
   */
  DECLARE_LUA_FUNCTION(VirtualHostWrapper, luaMetadata);

  const Protobuf::Struct& getMetadata() const;

  // Filters::Common::Lua::BaseLuaObject
  void onMarkDead() override { metadata_wrapper_.reset(); }

  const StreamInfo::StreamInfo& stream_info_;
  const absl::string_view filter_config_name_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::MetadataMapWrapper> metadata_wrapper_;
};

class RouteWrapper : public Filters::Common::Lua::BaseLuaObject<RouteWrapper> {
public:
  RouteWrapper(const StreamInfo::StreamInfo& stream_info,
               const absl::string_view filter_config_name)
      : stream_info_{stream_info}, filter_config_name_{filter_config_name} {}

  static ExportedFunctions exportedFunctions() { return {{"metadata", static_luaMetadata}}; }

private:
  /**
   * @return a handle to the metadata.
   */
  DECLARE_LUA_FUNCTION(RouteWrapper, luaMetadata);

  const Protobuf::Struct& getMetadata() const;

  // Filters::Common::Lua::BaseLuaObject
  void onMarkDead() override { metadata_wrapper_.reset(); }

  const StreamInfo::StreamInfo& stream_info_;
  const absl::string_view filter_config_name_;
  Filters::Common::Lua::LuaDeathRef<Filters::Common::Lua::MetadataMapWrapper> metadata_wrapper_;
};

/**
 * Lua wrapper for a stats Counter. Stores the stat name and re-queries the scope on each use
 * to avoid holding a reference that could outlive the stats store.
 */
class CounterWrapper : public Filters::Common::Lua::BaseLuaObject<CounterWrapper> {
public:
  CounterWrapper(Stats::Scope& scope, std::string name) : scope_(scope), name_(std::move(name)) {}

  static ExportedFunctions exportedFunctions() {
    return {{"inc", static_luaInc}, {"add", static_luaAdd}, {"value", static_luaValue}};
  }

private:
  /**
   * Increment the counter by 1.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(CounterWrapper, luaInc);

  /**
   * Add an amount to the counter.
   * @param 1 (int): amount to add.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(CounterWrapper, luaAdd);

  /**
   * Get the current value of the counter.
   * @return int current value.
   */
  DECLARE_LUA_FUNCTION(CounterWrapper, luaValue);

  Stats::Counter& counter() {
    return Stats::Utility::counterFromElements(scope_, {Stats::DynamicName(name_)});
  }

  Stats::Scope& scope_;
  const std::string name_;
};

/**
 * Lua wrapper for a stats Gauge. Stores the stat name and re-queries the scope on each use
 * to avoid holding a reference that could outlive the stats store.
 */
class GaugeWrapper : public Filters::Common::Lua::BaseLuaObject<GaugeWrapper> {
public:
  GaugeWrapper(Stats::Scope& scope, std::string name) : scope_(scope), name_(std::move(name)) {}

  static ExportedFunctions exportedFunctions() {
    return {{"inc", static_luaInc}, {"dec", static_luaDec}, {"add", static_luaAdd},
            {"sub", static_luaSub}, {"set", static_luaSet}, {"value", static_luaValue}};
  }

private:
  /**
   * Increment the gauge by 1.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaInc);

  /**
   * Decrement the gauge by 1.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaDec);

  /**
   * Add an amount to the gauge.
   * @param 1 (int): amount to add.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaAdd);

  /**
   * Subtract an amount from the gauge.
   * @param 1 (int): amount to subtract.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaSub);

  /**
   * Set the gauge to a specific value.
   * @param 1 (int): value to set.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaSet);

  /**
   * Get the current value of the gauge.
   * @return int current value.
   */
  DECLARE_LUA_FUNCTION(GaugeWrapper, luaValue);

  Stats::Gauge& gauge() {
    return Stats::Utility::gaugeFromElements(scope_, {Stats::DynamicName(name_)},
                                             Stats::Gauge::ImportMode::NeverImport);
  }

  Stats::Scope& scope_;
  const std::string name_;
};

/**
 * Lua wrapper for a stats Histogram. Stores the stat name and re-queries the scope on each use
 * to avoid holding a reference that could outlive the stats store.
 */
class HistogramWrapper : public Filters::Common::Lua::BaseLuaObject<HistogramWrapper> {
public:
  HistogramWrapper(Stats::Scope& scope, std::string name, Stats::Histogram::Unit unit)
      : scope_(scope), name_(std::move(name)), unit_(unit) {}

  static ExportedFunctions exportedFunctions() { return {{"recordValue", static_luaRecordValue}}; }

private:
  /**
   * Record a value in the histogram.
   * @param 1 (int): value to record.
   * @return nothing.
   */
  DECLARE_LUA_FUNCTION(HistogramWrapper, luaRecordValue);

  Stats::Histogram& histogram() {
    return Stats::Utility::histogramFromElements(scope_, {Stats::DynamicName(name_)}, unit_);
  }

  Stats::Scope& scope_;
  const std::string name_;
  const Stats::Histogram::Unit unit_;
};

/**
 * Lua wrapper for a stats Scope. Allows Lua scripts to create and access
 * counters, gauges, and histograms.
 */
class StatsScopeWrapper : public Filters::Common::Lua::BaseLuaObject<StatsScopeWrapper> {
public:
  explicit StatsScopeWrapper(Stats::Scope& scope) : scope_(scope) {}

  static ExportedFunctions exportedFunctions() {
    return {{"counter", static_luaCounter},
            {"gauge", static_luaGauge},
            {"histogram", static_luaHistogram}};
  }

private:
  /**
   * Get or create a counter with the given name.
   * @param 1 (string): counter name.
   * @return CounterWrapper handle.
   */
  DECLARE_LUA_FUNCTION(StatsScopeWrapper, luaCounter);

  /**
   * Get or create a gauge with the given name.
   * @param 1 (string): gauge name.
   * @return GaugeWrapper handle.
   */
  DECLARE_LUA_FUNCTION(StatsScopeWrapper, luaGauge);

  /**
   * Get or create a histogram with the given name.
   * @param 1 (string): histogram name.
   * @param 2 (string, optional): unit - "ms"/"milliseconds", "microseconds", "bytes", or
   *                              "unspecified" (default).
   * @return HistogramWrapper handle.
   */
  DECLARE_LUA_FUNCTION(StatsScopeWrapper, luaHistogram);

  Stats::Scope& scope_;
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
