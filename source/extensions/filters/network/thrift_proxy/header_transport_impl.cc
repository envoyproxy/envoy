#include "extensions/filters/network/thrift_proxy/header_transport_impl.h"

#include <limits>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

// c.f.
// https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/protocol/TProtocolTypes.h#L27
enum class HeaderProtocolType {
  Binary = 0,
  JSON = 1,
  Compact = 2,

  FirstHeaderProtocolType = Binary,
  LastHeaderProtocolType = Compact,
};

// Fixed portion of frame header:
//   Header magic: 2 bytes +
//   Flags: 2 bytes +
//   Sequence number: 4 bytes
//   Header data size: 2 bytes
constexpr uint64_t MinFrameStartSizeNoHeaders = 10;

// Minimum frame size: fixed portion of frame header + 4 bytes of header data (the minimum)
constexpr int32_t MinFrameStartSize = MinFrameStartSizeNoHeaders + 4;

// Minimum to start decoding: 4 bytes of frame size + the fixed portion of the frame header
constexpr uint64_t MinDecodeBytes = MinFrameStartSizeNoHeaders + 4;

// Maximum size for header data.
constexpr int32_t MaxHeadersSize = 65536;

} // namespace

bool HeaderTransportImpl::decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) {
  if (buffer.length() < MinDecodeBytes) {
    return false;
  }

  // Size of frame, not including the length bytes.
  const int32_t frame_size = buffer.peekBEInt<int32_t>();

  // Minimum header frame size is 18 bytes (4 bytes of frame size + 10 bytes of fixed header +
  // minimum 4 bytes of variable header data), so frame_size must be at least 14.
  if (frame_size < MinFrameStartSize || frame_size > MaxFrameSize) {
    throw EnvoyException(absl::StrCat("invalid thrift header transport frame size ", frame_size));
  }

  int16_t magic = buffer.peekBEInt<uint16_t>(4);
  if (!isMagic(magic)) {
    throw EnvoyException(fmt::format("invalid thrift header transport magic {:04x}", magic));
  }

  // offset 6: 16 bit flags field, unused
  // offset 8: 32 bit sequence number field
  int32_t seq_id = buffer.peekBEInt<int32_t>(8);

  // offset 12: 16 bit (remaining) header size / 4 (spec erroneously claims / 32).
  int16_t raw_header_size = buffer.peekBEInt<int16_t>(12);
  int32_t header_size = static_cast<int32_t>(raw_header_size) * 4;
  if (header_size < 0 || header_size > MaxHeadersSize) {
    throw EnvoyException(fmt::format("invalid thrift header transport header size {} ({:04x})",
                                     header_size, static_cast<uint16_t>(raw_header_size)));
  }

  if (header_size == 0) {
    throw EnvoyException("no header data");
  }

  if (buffer.length() < static_cast<uint64_t>(header_size) + MinDecodeBytes) {
    // Need more header data.
    return false;
  }

  // Header data starts at offset 14 (4 bytes of frame size followed by 10 bytes of fixed header).
  buffer.drain(MinDecodeBytes);

  // Remaining frame size is the original frame size (which does not count itself), less the 10
  // fixed bytes of the header (magic, flags, etc), less the size of the variable header data
  // (header_size).
  metadata.setFrameSize(
      static_cast<uint32_t>(frame_size - header_size - MinFrameStartSizeNoHeaders));
  metadata.setSequenceId(seq_id);

  ProtocolType proto = ProtocolType::Auto;
  HeaderProtocolType header_proto =
      static_cast<HeaderProtocolType>(drainVarIntI16(buffer, header_size, "protocol id"));
  switch (header_proto) {
  case HeaderProtocolType::Binary:
    proto = ProtocolType::Binary;
    break;
  case HeaderProtocolType::Compact:
    proto = ProtocolType::Compact;
    break;
  default:
    throw EnvoyException(fmt::format("Unknown protocol {}", static_cast<int>(header_proto)));
  }
  metadata.setProtocol(proto);

  int16_t num_xforms = drainVarIntI16(buffer, header_size, "transform count");
  if (num_xforms < 0) {
    throw EnvoyException(absl::StrCat("invalid header transport transform count ", num_xforms));
  }

  while (num_xforms-- > 0) {
    int32_t xform_id = drainVarIntI32(buffer, header_size, "transform id");

    // To date, no transforms have a data field. In the future, some transform IDs may require
    // consuming another varint 32 at this point. The known transform IDs are:
    // 1: zlib compression
    // 2: hmac (appended to end of packet)
    // 3: snappy compression
    buffer.drain(header_size);
    metadata.setAppException(AppExceptionType::MissingResult,
                             absl::StrCat("Unknown transform ", xform_id));
    return true;
  }

  while (header_size > 0) {
    // Attempt to read info blocks
    int32_t info_id = drainVarIntI32(buffer, header_size, "info id");
    if (info_id != 1) {
      // 0 indicates a padding byte, and the end of the info block.
      // 1 indicates an info id header/value pair.
      // Any other value is an unknown info id block, which we ignore.
      break;
    }

    int32_t num_headers = drainVarIntI32(buffer, header_size, "header count");
    if (num_headers < 0) {
      throw EnvoyException(absl::StrCat("invalid header transport header count ", num_headers));
    }

    while (num_headers-- > 0) {
      const Http::LowerCaseString key =
          Http::LowerCaseString(drainVarString(buffer, header_size, "header key"));
      const std::string value = drainVarString(buffer, header_size, "header value");
      metadata.headers().addCopy(key, value);
    }
  }

  // Remaining bytes are padding or ignored info blocks.
  if (header_size > 0) {
    buffer.drain(header_size);
  }

  return true;
}

bool HeaderTransportImpl::decodeFrameEnd(Buffer::Instance&) {
  exception_.reset();
  exception_reason_.clear();

  return true;
}

void HeaderTransportImpl::encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                                      Buffer::Instance& message) {
  uint64_t msg_size = message.length();
  if (msg_size == 0) {
    throw EnvoyException(absl::StrCat("invalid thrift header transport message size ", msg_size));
  }

  const Http::HeaderMap& headers = metadata.headers();
  if (headers.size() > MaxHeadersSize / 2) {
    // Each header takes a minimum of 2 bytes, yielding this limit.
    throw EnvoyException(
        absl::StrCat("invalid thrift header transport too many headers ", headers.size()));
  }

  Buffer::OwnedImpl header_buffer;

  if (!metadata.hasProtocol()) {
    throw EnvoyException("missing header transport protocol");
  }

  switch (metadata.protocol()) {
  case ProtocolType::Binary:
    BufferHelper::writeVarIntI32(header_buffer, static_cast<int32_t>(HeaderProtocolType::Binary));
    break;
  case ProtocolType::Compact:
    BufferHelper::writeVarIntI32(header_buffer, static_cast<int32_t>(HeaderProtocolType::Compact));
    break;
  default:
    throw EnvoyException(fmt::format("invalid header transport protocol {}",
                                     ProtocolNames::get().fromType(metadata.protocol())));
  }

  BufferHelper::writeVarIntI32(header_buffer, 0); // num transforms
  if (!headers.empty()) {
    // Info ID 1
    header_buffer.writeByte(1);

    // Num headers
    BufferHelper::writeVarIntI32(header_buffer, static_cast<int32_t>(headers.size()));

    headers.iterate(
        [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
          Buffer::Instance* hb = static_cast<Buffer::Instance*>(context);
          writeVarString(*hb, header.key().getStringView());
          writeVarString(*hb, header.value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        },
        &header_buffer);
  }

  uint64_t header_size = header_buffer.length();

  // Always pad (as the Apache implementation does).
  const int padding = 4 - (header_size % 4);
  header_buffer.add("\0\0\0\0", padding);
  header_size += padding;

  if (header_size > MaxHeadersSize) {
    throw EnvoyException(absl::StrCat("invalid thrift header transport header size ", header_size));
  }

  // Frame size does not include the frame length itself.
  uint64_t size = header_size + msg_size + MinFrameStartSizeNoHeaders;
  if (size > MaxFrameSize) {
    throw EnvoyException(absl::StrCat("invalid thrift header transport frame size ", size));
  }

  int32_t seq_id = 0;
  if (metadata.hasSequenceId()) {
    seq_id = metadata.sequenceId();
  }

  buffer.writeBEInt<uint32_t>(static_cast<uint32_t>(size));
  buffer.writeBEInt<uint16_t>(Magic);
  buffer.writeBEInt<uint16_t>(0); // flags
  buffer.writeBEInt<int32_t>(seq_id);
  buffer.writeBEInt<uint16_t>(static_cast<uint16_t>(header_size / 4));

  buffer.move(header_buffer);
  buffer.move(message);
}

int16_t HeaderTransportImpl::drainVarIntI16(Buffer::Instance& buffer, int32_t& header_size,
                                            const char* desc) {
  int32_t value = drainVarIntI32(buffer, header_size, desc);
  if (value > static_cast<int32_t>(std::numeric_limits<int16_t>::max())) {
    throw EnvoyException(fmt::format("header transport {}: value {} exceeds max i16 ({})", desc,
                                     value, std::numeric_limits<int16_t>::max()));
  }
  return static_cast<int16_t>(value);
}

int32_t HeaderTransportImpl::drainVarIntI32(Buffer::Instance& buffer, int32_t& header_size,
                                            const char* desc) {
  if (header_size <= 0) {
    throw EnvoyException(fmt::format("unable to read header transport {}: header too small", desc));
  }

  int size;
  int32_t value = BufferHelper::peekVarIntI32(buffer, 0, size);
  if (size < 0 || (header_size - size) < 0) {
    throw EnvoyException(fmt::format("unable to read header transport {}: header too small", desc));
  }
  buffer.drain(size);
  header_size -= size;
  return value;
}

std::string HeaderTransportImpl::drainVarString(Buffer::Instance& buffer, int32_t& header_size,
                                                const char* desc) {
  const int16_t str_len = drainVarIntI16(buffer, header_size, desc);
  if (str_len == 0) {
    return "";
  }

  if (header_size < static_cast<int32_t>(str_len)) {
    throw EnvoyException(fmt::format("unable to read header transport {}: header too small", desc));
  }

  const std::string value(static_cast<char*>(buffer.linearize(str_len)), str_len);
  buffer.drain(str_len);
  header_size -= str_len;
  return value;
}

void HeaderTransportImpl::writeVarString(Buffer::Instance& buffer, const absl::string_view str) {
  const std::string::size_type len = str.length();
  if (len > static_cast<uint32_t>(std::numeric_limits<int16_t>::max())) {
    throw EnvoyException(absl::StrCat("header string too long: ", len));
  }

  BufferHelper::writeVarIntI32(buffer, static_cast<int32_t>(len));
  if (len == 0) {
    return;
  }
  buffer.add(str.data(), len);
}

class HeaderTransportConfigFactory : public TransportFactoryBase<HeaderTransportImpl> {
public:
  HeaderTransportConfigFactory() : TransportFactoryBase(TransportNames::get().HEADER) {}
};

/**
 * Static registration for the header transport. @see RegisterFactory.
 */
REGISTER_FACTORY(HeaderTransportConfigFactory, NamedTransportConfigFactory);

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
