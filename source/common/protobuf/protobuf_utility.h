#pragma once

#include "common/common/hash.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
// class ProtobufMessageHash {
// public:
/**
 * A hash function uses Protobuf::TextFormat to force deterministic serialization recursively
 * including known types in google.protobuf.Any. See
 * https://github.com/protocolbuffers/protobuf/issues/5731 for the context.
 * Using this function is discouraged, see discussion in
 * https://github.com/envoyproxy/envoy/issues/8301.
 */
// static std::size_t hash(const Protobuf::Message& message);
//} ;

class ValueUtil {
public:
  static std::size_t hash(const ProtobufWkt::Value& value) {
    std::string text_format;
    {
      Protobuf::TextFormat::Printer printer;
      printer.SetExpandAny(true);
      printer.SetUseFieldNumber(true);
      printer.SetSingleLineMode(true);
      printer.PrintToString(value, &text_format);
    }

    return HashUtil::xxHash64(text_format);
  }

  /**
   * Load YAML string into ProtobufWkt::Value.
   */
  static ProtobufWkt::Value loadFromYaml(const std::string& yaml);

  /**
   * Compare two ProtobufWkt::Values for equality.
   * @param v1 message of type type.googleapis.com/google.protobuf.Value
   * @param v2 message of type type.googleapis.com/google.protobuf.Value
   * @return true if v1 and v2 are identical
   */
static bool equal(const ProtobufWkt::Value& v1, const ProtobufWkt::Value& v2);

  /**
   * @return wrapped ProtobufWkt::NULL_VALUE.
   */
  static const ProtobufWkt::Value& nullValue();

  /**
   * Wrap std::string into ProtobufWkt::Value string value.
   * @param str string to be wrapped.
   * @return wrapped string.
   */
  static ProtobufWkt::Value stringValue(const std::string& str);

  /**
   * Wrap boolean into ProtobufWkt::Value boolean value.
   * @param str boolean to be wrapped.
   * @return wrapped boolean.
   */
  static ProtobufWkt::Value boolValue(bool b);

  /**
  * Wrap ProtobufWkt::Struct into ProtobufWkt::Value struct value.
   * @param obj struct to be wrapped.
   * @return wrapped struct.
   */
  static ProtobufWkt::Value structValue(const ProtobufWkt::Struct& obj);

  /**
   * Wrap number into ProtobufWkt::Value double value.
   * @param num number to be wrapped.
   * @return wrapped number.
   */
  template <typename T> static ProtobufWkt::Value numberValue(const T num) {
    ProtobufWkt::Value val;
    val.set_number_value(static_cast<double>(num));
    return val;
  }

  /**
   * Wrap a collection of ProtobufWkt::Values into ProtobufWkt::Value list value.
   * @param values collection of ProtobufWkt::Values to be wrapped.
   * @return wrapped list value.
   */
static ProtobufWkt::Value listValue(const std::vector<ProtobufWkt::Value>& values);
};

/**
 * HashedValue is a wrapper around ProtobufWkt::Value that computes
 * and stores a hash code for the Value at construction.
 */
class HashedValue {
public:
  HashedValue(const ProtobufWkt::Value& value) : value_(value), hash_(ValueUtil::hash(value)){};
  HashedValue(const HashedValue& v) = default;

  const ProtobufWkt::Value& value() const { return value_; }
  std::size_t hash() const { return hash_; }

  bool operator==(const HashedValue& rhs) const {
    return hash_ == rhs.hash_ && ValueUtil::equal(value_, rhs.value_);
  }

  bool operator!=(const HashedValue& rhs) const { return !(*this == rhs); }
private:
  const ProtobufWkt::Value value_;
  const std::size_t hash_;
};

} // namespace Envoy

namespace std {
// Inject an implementation of std::hash for Envoy::HashedValue into the std namespace.
template <> struct hash<Envoy::HashedValue> {
  std::size_t operator()(Envoy::HashedValue const& v) const { return v.hash(); }
};

} // namespace std

