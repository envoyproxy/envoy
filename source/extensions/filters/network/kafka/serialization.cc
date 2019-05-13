#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

constexpr static int16_t NULL_STRING_LENGTH = -1;
constexpr static int32_t NULL_BYTES_LENGTH = -1;

/**
 * Helper method for deserializers that get the length of data, and then copy the given bytes into a
 * local buffer. Templated as there are length and byte type differences. Impl note: This method
 * modifies (sets up) most of Deserializer's fields.
 * @param data bytes to deserialize.
 * @param length_deserializer payload length deserializer.
 * @param length_consumed_marker marker telling whether length has been extracted from
 * length_deserializer, and underlying buffer has been initialized.
 * @param required remaining bytes to consume.
 * @param data_buffer buffer with capacity for 'required' bytes.
 * @param ready marker telling whether this deserialized has finished processing.
 * @param null_value_length value marking null values.
 * @param allow_null_value whether null value if allowed.
 * @return number of bytes consumed.
 */
template <typename DeserializerType, typename LengthType, typename ByteType>
uint32_t feedBytesIntoBuffers(absl::string_view& data, DeserializerType& length_deserializer,
                              bool& length_consumed_marker, LengthType& required,
                              std::vector<ByteType>& data_buffer, bool& ready,
                              const LengthType null_value_length, const bool allow_null_value) {

  const uint32_t length_consumed = length_deserializer.feed(data);
  if (!length_deserializer.ready()) {
    // Break early: we still need to fill in length buffer.
    return length_consumed;
  }

  if (!length_consumed_marker) {
    // Length buffer is ready, but we have not yet processed the result.
    // We need to extract the real data length and initialize buffer for it.
    required = length_deserializer.get();

    if (required >= 0) {
      data_buffer = std::vector<ByteType>(required);
    }

    if (required == null_value_length) {
      if (allow_null_value) {
        // We have received 'null' value in deserializer that allows it (e.g. NullableBytes), no
        // more processing is necessary.
        ready = true;
      } else {
        // Invalid payload: null length for non-null object.
        throw EnvoyException(fmt::format("invalid length: {}", required));
      }
    }

    if (required < null_value_length) {
      throw EnvoyException(fmt::format("invalid length: {}", required));
    }

    length_consumed_marker = true;
  }

  if (ready) {
    // Break early: we might not need to consume any bytes for nullable values OR in case of repeat
    // invocation on already-ready buffer.
    return length_consumed;
  }

  const uint32_t data_consumed = std::min<uint32_t>(required, data.size());
  const uint32_t written = data_buffer.size() - required;
  if (data_consumed > 0) {
    memcpy(data_buffer.data() + written, data.data(), data_consumed);
    required -= data_consumed;
    data = {data.data() + data_consumed, data.size() - data_consumed};
  }

  // We have consumed all the bytes, mark the deserializer as ready.
  if (required == 0) {
    ready = true;
  }

  return length_consumed + data_consumed;
}

uint32_t StringDeserializer::feed(absl::string_view& data) {
  return feedBytesIntoBuffers<Int16Deserializer, int16_t, char>(
      data, length_buf_, length_consumed_, required_, data_buf_, ready_, NULL_STRING_LENGTH, false);
}

uint32_t NullableStringDeserializer::feed(absl::string_view& data) {
  return feedBytesIntoBuffers<Int16Deserializer, int16_t, char>(
      data, length_buf_, length_consumed_, required_, data_buf_, ready_, NULL_STRING_LENGTH, true);
}

uint32_t BytesDeserializer::feed(absl::string_view& data) {
  return feedBytesIntoBuffers<Int32Deserializer, int32_t, unsigned char>(
      data, length_buf_, length_consumed_, required_, data_buf_, ready_, NULL_BYTES_LENGTH, false);
}

uint32_t NullableBytesDeserializer::feed(absl::string_view& data) {
  return feedBytesIntoBuffers<Int32Deserializer, int32_t, unsigned char>(
      data, length_buf_, length_consumed_, required_, data_buf_, ready_, NULL_BYTES_LENGTH, true);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
