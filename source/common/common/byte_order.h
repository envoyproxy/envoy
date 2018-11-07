#pragma once

#include <cstddef>
#include <cstdint>

// NOLINT(namespace-envoy)

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#define htole16(x) OSSwapHostToLittleInt16((x))
#define htole32(x) OSSwapHostToLittleInt32((x))
#define htole64(x) OSSwapHostToLittleInt64((x))
#define le16toh(x) OSSwapLittleToHostInt16((x))
#define le32toh(x) OSSwapLittleToHostInt32((x))
#define le64toh(x) OSSwapLittleToHostInt64((x))

#define htobe16(x) OSSwapHostToBigInt16((x))
#define htobe32(x) OSSwapHostToBigInt32((x))
#define htobe64(x) OSSwapHostToBigInt64((x))
#define be16toh(x) OSSwapBigToHostInt16((x))
#define be32toh(x) OSSwapBigToHostInt32((x))
#define be64toh(x) OSSwapBigToHostInt64((x))

#else

#include <endian.h>

#endif

enum class ByteOrder { Host, LittleEndian, BigEndian };

template <ByteOrder, typename Integral, size_t = sizeof(Integral)> struct EndiannessConverter;

// convenience function that converts an integer from host byte-order to a specified endianness
template <ByteOrder Endianness, typename T> inline T toEndianness(T value) {
  return EndiannessConverter<Endianness, T>::to(value);
}

// convenience function that converts an integer from a specified endianness to host byte-order
template <ByteOrder Endianness, typename T> inline T fromEndianness(T value) {
  return EndiannessConverter<Endianness, T>::from(value);
}

// Implementation details below

// implementation details of EndiannessConverter for 8-bit host endianness integers
template <typename T> struct EndiannessConverter<ByteOrder::Host, T, sizeof(uint8_t)> {
  static_assert(sizeof(T) == sizeof(uint8_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 16-bit host endianness integers
template <typename T> struct EndiannessConverter<ByteOrder::Host, T, sizeof(uint16_t)> {
  static_assert(sizeof(T) == sizeof(uint16_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 32-bit host endianness integers
template <typename T> struct EndiannessConverter<ByteOrder::Host, T, sizeof(uint32_t)> {
  static_assert(sizeof(T) == sizeof(uint32_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 64-bit host endianness integers
template <typename T> struct EndiannessConverter<ByteOrder::Host, T, sizeof(uint64_t)> {
  static_assert(sizeof(T) == sizeof(uint64_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 8-bit little endian integers
template <typename T> struct EndiannessConverter<ByteOrder::LittleEndian, T, sizeof(uint8_t)> {
  static_assert(sizeof(T) == sizeof(uint8_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 16-bit little endian integers
template <typename T> struct EndiannessConverter<ByteOrder::LittleEndian, T, sizeof(uint16_t)> {
  static_assert(sizeof(T) == sizeof(uint16_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htole16(static_cast<uint16_t>(value))); }

  static T from(T value) { return static_cast<T>(le16toh(static_cast<uint16_t>(value))); }
};

// implementation details of EndiannessConverter for 32-bit little endian integers
template <typename T> struct EndiannessConverter<ByteOrder::LittleEndian, T, sizeof(uint32_t)> {
  static_assert(sizeof(T) == sizeof(uint32_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htole32(static_cast<uint32_t>(value))); }

  static T from(T value) { return static_cast<T>(le32toh(static_cast<uint32_t>(value))); }
};

// implementation details of EndiannessConverter for 64-bit little endian integers
template <typename T> struct EndiannessConverter<ByteOrder::LittleEndian, T, sizeof(uint64_t)> {
  static_assert(sizeof(T) == sizeof(uint64_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htole64(static_cast<uint64_t>(value))); }

  static T from(T value) { return static_cast<T>(le64toh(static_cast<uint64_t>(value))); }
};

// implementation details of EndiannessConverter for 8-bit big endian integers
template <typename T> struct EndiannessConverter<ByteOrder::BigEndian, T, sizeof(uint8_t)> {
  static_assert(sizeof(T) == sizeof(uint8_t), "incorrect type width");

  static T to(T value) { return value; }

  static T from(T value) { return value; }
};

// implementation details of EndiannessConverter for 16-bit big endian integers
template <typename T> struct EndiannessConverter<ByteOrder::BigEndian, T, sizeof(uint16_t)> {
  static_assert(sizeof(T) == sizeof(uint16_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htobe16(static_cast<uint16_t>(value))); }

  static T from(T value) { return static_cast<T>(be16toh(static_cast<uint16_t>(value))); }
};

// implementation details of EndiannessConverter for 32-bit big endian integers
template <typename T> struct EndiannessConverter<ByteOrder::BigEndian, T, sizeof(uint32_t)> {
  static_assert(sizeof(T) == sizeof(uint32_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htobe32(static_cast<uint32_t>(value))); }

  static T from(T value) { return static_cast<T>(be32toh(static_cast<uint32_t>(value))); }
};

// implementation details of EndiannessConverter for 64-bit big endian integers
template <typename T> struct EndiannessConverter<ByteOrder::BigEndian, T, sizeof(uint64_t)> {
  static_assert(sizeof(T) == sizeof(uint64_t), "incorrect type width");

  static T to(T value) { return static_cast<T>(htobe64(static_cast<uint64_t>(value))); }

  static T from(T value) { return static_cast<T>(be64toh(static_cast<uint64_t>(value))); }
};
