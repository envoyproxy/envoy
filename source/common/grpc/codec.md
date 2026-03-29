# gRPC Codec - Frame Encoding and Decoding

**File**: `codec.h/cc`
**Purpose**: gRPC wire format encoding/decoding (5-byte header + payload)

## Overview

The gRPC codec handles:
- **Frame encoding** - Prepending 5-byte header to protobuf messages
- **Frame decoding** - Parsing 5-byte header and extracting payloads
- **Compression support** - Gzip compressed messages
- **Streaming** - Incremental frame parsing
- **Error handling** - Oversized frames, malformed data

## gRPC Wire Format

### Frame Structure

```
┌──────────┬────────────────────────────────────┬─────────────────┐
│  Flags   │          Length                    │     Payload     │
│ (1 byte) │      (4 bytes, big-endian)         │ (Length bytes)  │
└──────────┴────────────────────────────────────┴─────────────────┘
     ↑                    ↑                              ↑
   Byte 0             Bytes 1-4                      Bytes 5+
```

### Flags Byte

```cpp
const uint8_t GRPC_FH_DEFAULT = 0b00000000;      // Uncompressed
const uint8_t GRPC_FH_COMPRESSED = 0b00000001;   // Compressed with gzip
const uint8_t CONNECT_FH_EOS = 0b00000010;       // End-of-stream (Connect protocol)
```

**Bit Layout**:
```
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ R │ R │ R │ R │ R │ R │EOS│ C │
└───┴───┴───┴───┴───┴───┴───┴───┘
                         │   │
                         │   └─ Compression (0=no, 1=yes)
                         └───── End-of-stream for Connect

R = Reserved for future use
```

### Length Field

- **4 bytes** in **big-endian** format
- **Maximum**: 2^32 - 1 bytes (4 GB)
- **Typical**: Few KB to few MB

### Example Frame

**Request**: `{ "name": "Alice" }`

1. **Protobuf serialization**: `\x0a\x05Alice` (7 bytes)
2. **Frame**:
   ```
   [0x00]                    // Flags: uncompressed
   [0x00, 0x00, 0x00, 0x07]  // Length: 7 (big-endian)
   [\x0a\x05Alice]           // Payload: 7 bytes
   ```

## Key Classes

### Encoder

Prepends gRPC frame header to messages:

```cpp
class Encoder {
public:
    Encoder();

    // Create frame header (5 bytes)
    void newFrame(
        uint8_t flags,
        uint64_t length,
        std::array<uint8_t, 5>& output
    );

    // Prepend frame header to buffer
    void prependFrameHeader(uint8_t flags, Buffer::Instance& buffer);

    // Prepend frame header with explicit length
    void prependFrameHeader(
        uint8_t flags,
        Buffer::Instance& buffer,
        uint32_t message_length
    );
};
```

### Decoder

Parses gRPC frames from buffer:

```cpp
class Decoder : public FrameInspector {
public:
    // Decode frames from input buffer
    absl::Status decode(
        Buffer::Instance& input,        // Input buffer (consumed)
        std::vector<Frame>& output      // Decoded frames
    );

    // Get current frame length
    uint32_t length() const { return frame_.length_; }

    // Check if decoder has buffered partial data
    bool hasBufferedData() const { return state_ != State::FhFlag; }

    // Set maximum frame length (for protection)
    void setMaxFrameLength(uint32_t max_frame_length);

private:
    Frame frame_;                       // Current frame being decoded
    std::vector<Frame>* output_{nullptr};
    bool decoding_error_{false};
};
```

### Frame

Decoded frame structure:

```cpp
struct Frame {
    uint8_t flags_;                  // Frame flags
    uint32_t length_;                // Payload length
    Buffer::InstancePtr data_;       // Payload data
};
```

### Decoder States

```cpp
enum class State {
    FhFlag,   // Reading flags byte
    FhLen0,   // Reading length byte 0 (MSB)
    FhLen1,   // Reading length byte 1
    FhLen2,   // Reading length byte 2
    FhLen3,   // Reading length byte 3 (LSB)
    Data,     // Reading payload data
};
```

## Encoding

### Basic Encoding

```cpp
void encodeMessage(const Protobuf::Message& message, Buffer::Instance& output) {
    // 1. Serialize protobuf
    std::string serialized;
    message.SerializeToString(&serialized);

    // 2. Add to buffer
    output.add(serialized);

    // 3. Prepend gRPC frame header
    Encoder encoder;
    encoder.prependFrameHeader(GRPC_FH_DEFAULT, output);
    // Buffer now: [5-byte header][protobuf payload]
}
```

### With Compression

```cpp
void encodeCompressedMessage(
    const Protobuf::Message& message,
    Buffer::Instance& output
) {
    // 1. Serialize protobuf
    std::string serialized;
    message.SerializeToString(&serialized);

    // 2. Compress with gzip
    Buffer::OwnedImpl compressed;
    Envoy::Compression::Compressor compressor;
    compressor.compress(
        Buffer::OwnedImpl(serialized),
        compressed
    );

    // 3. Add compressed data
    output.move(compressed);

    // 4. Prepend frame header with compression flag
    Encoder encoder;
    encoder.prependFrameHeader(GRPC_FH_COMPRESSED, output);
}
```

### Encoder Implementation

```cpp
void Encoder::prependFrameHeader(uint8_t flags, Buffer::Instance& buffer) {
    prependFrameHeader(flags, buffer, buffer.length());
}

void Encoder::prependFrameHeader(
    uint8_t flags,
    Buffer::Instance& buffer,
    uint32_t message_length
) {
    // Create header
    std::array<uint8_t, 5> header;
    newFrame(flags, message_length, header);

    // Prepend to buffer
    buffer.prepend(absl::string_view(
        reinterpret_cast<const char*>(header.data()),
        5
    ));
}

void Encoder::newFrame(
    uint8_t flags,
    uint64_t length,
    std::array<uint8_t, 5>& output
) {
    ASSERT(length <= std::numeric_limits<uint32_t>::max());

    // Byte 0: Flags
    output[0] = flags;

    // Bytes 1-4: Length (big-endian)
    uint32_t len = static_cast<uint32_t>(length);
    output[1] = (len >> 24) & 0xFF;  // MSB
    output[2] = (len >> 16) & 0xFF;
    output[3] = (len >> 8) & 0xFF;
    output[4] = len & 0xFF;          // LSB
}
```

## Decoding

### State Machine

The decoder uses a state machine to incrementally parse frames:

```
     ┌─────────┐
     │ FhFlag  │  ← Waiting for flags byte
     └────┬────┘
          ↓ (got flags)
     ┌─────────┐
     │ FhLen0  │  ← Reading length byte 0 (MSB)
     └────┬────┘
          ↓ (got byte 0)
     ┌─────────┐
     │ FhLen1  │  ← Reading length byte 1
     └────┬────┘
          ↓ (got byte 1)
     ┌─────────┐
     │ FhLen2  │  ← Reading length byte 2
     └────┬────┘
          ↓ (got byte 2)
     ┌─────────┐
     │ FhLen3  │  ← Reading length byte 3 (LSB)
     └────┬────┘
          ↓ (got byte 3, now know length)
     ┌─────────┐
     │  Data   │  ← Reading payload (length bytes)
     └────┬────┘
          ↓ (got all data)
          ↑──── Back to FhFlag for next frame
```

### Basic Decoding

```cpp
void decodeMessages(Buffer::Instance& input, std::vector<Frame>& output) {
    Decoder decoder;

    // Decode all complete frames
    absl::Status status = decoder.decode(input, output);

    if (!status.ok()) {
        // Decoding error
        throw Grpc::Exception(
            Grpc::Status::WellKnownGrpcStatus::Internal,
            "Frame decode error"
        );
    }

    // Process frames
    for (auto& frame : output) {
        // Check compression
        if (frame.flags_ & GRPC_FH_COMPRESSED) {
            // Decompress
            Buffer::OwnedImpl decompressed;
            decompressor_.decompress(*frame.data_, decompressed);
            frame.data_ = std::make_unique<Buffer::OwnedImpl>();
            frame.data_->move(decompressed);
        }

        // Parse protobuf
        MyMessage message;
        if (!message.ParseFromArray(
                frame.data_->linearize(frame.data_->length()),
                frame.data_->length())) {
            // Parse error
        }
    }
}
```

### Decoder Implementation

```cpp
absl::Status Decoder::decode(
    Buffer::Instance& input,
    std::vector<Frame>& output
) {
    output_ = &output;
    decoding_error_ = false;

    // Inspect buffer (processes as much as possible)
    inspect(input);

    if (decoding_error_) {
        return absl::InvalidArgumentError("Frame decode error");
    }

    return absl::OkStatus();
}

bool Decoder::frameStart(uint8_t flags) {
    // Start of new frame
    frame_.flags_ = flags;
    frame_.data_ = std::make_unique<Buffer::OwnedImpl>();
    return true;
}

void Decoder::frameDataStart() {
    // Check for oversized frame
    if (max_frame_length_ > 0 && frame_.length_ > max_frame_length_) {
        is_frame_oversized_ = true;
        decoding_error_ = true;
    }
}

void Decoder::frameData(uint8_t* data, uint64_t length) {
    // Add data to frame
    if (!is_frame_oversized_) {
        frame_.data_->add(data, length);
    }
}

void Decoder::frameDataEnd() {
    // Complete frame - add to output
    if (!is_frame_oversized_) {
        output_->push_back(std::move(frame_));
    }

    // Reset for next frame
    frame_ = Frame{};
    is_frame_oversized_ = false;
}
```

### Incremental Decoding

The decoder handles partial data gracefully:

```cpp
// Scenario: 100 bytes arrive, but frame is 200 bytes
Buffer::OwnedImpl input;
input.add(/* 100 bytes */);

std::vector<Frame> output;
decoder.decode(input, output);  // Buffers partial frame internally

// Later: remaining 100 bytes arrive
input.add(/* 100 more bytes */);
decoder.decode(input, output);  // Completes frame, adds to output
```

## Frame Inspector

Base class for observing frames without decoding:

```cpp
class FrameInspector {
public:
    // Inspect buffer and count frames
    uint64_t inspect(const Buffer::Instance& input);

    // Get frame count
    uint64_t frameCount() const { return count_; }

    // Get current parse state
    State state() const { return state_; }

protected:
    // Override to observe frame events
    virtual bool frameStart(uint8_t flags) { return true; }
    virtual void frameDataStart() {}
    virtual void frameData(uint8_t* data, uint64_t length) {}
    virtual void frameDataEnd() {}

    State state_{State::FhFlag};
    uint32_t length_{0};
    uint64_t count_{0};
    uint32_t max_frame_length_{0};
};
```

**Use case**: Count frames without allocating/copying data:

```cpp
class FrameCounter : public Grpc::FrameInspector {
protected:
    bool frameStart(uint8_t flags) override {
        frame_count_++;
        return false;  // Don't need to inspect data
    }

private:
    size_t frame_count_{0};
};
```

## Usage Examples

### Example 1: Encode Single Message

```cpp
void sendGrpcMessage(const MyRequest& request, Http::AsyncClient::Stream& stream) {
    // 1. Serialize protobuf
    Buffer::OwnedImpl buffer;
    std::string serialized;
    request.SerializeToString(&serialized);
    buffer.add(serialized);

    // 2. Prepend gRPC frame header
    Grpc::Encoder encoder;
    encoder.prependFrameHeader(Grpc::GRPC_FH_DEFAULT, buffer);

    // 3. Send via HTTP/2
    stream.sendData(buffer, false);
}
```

### Example 2: Decode Multiple Messages

```cpp
void onData(Buffer::Instance& data, bool end_stream) {
    std::vector<Grpc::Frame> frames;

    // Decode all frames in buffer
    auto status = decoder_.decode(data, frames);
    if (!status.ok()) {
        handleDecodeError();
        return;
    }

    // Process each frame
    for (auto& frame : frames) {
        MyResponse response;
        if (response.ParseFromArray(
                frame.data_->linearize(frame.length_),
                frame.length_)) {
            handleResponse(response);
        }
    }
}
```

### Example 3: Streaming with Buffering

```cpp
class StreamHandler {
public:
    void onData(Buffer::Instance& data, bool end_stream) {
        // Accumulate data
        buffer_.move(data);

        // Decode frames
        std::vector<Grpc::Frame> frames;
        decoder_.decode(buffer_, frames);

        for (auto& frame : frames) {
            processFrame(std::move(frame));
        }

        // buffer_ now contains only partial frame (if any)
    }

private:
    Buffer::OwnedImpl buffer_;
    Grpc::Decoder decoder_;
};
```

### Example 4: Frame Size Limiting

```cpp
// Protect against oversized frames
Grpc::Decoder decoder;
decoder.setMaxFrameLength(4 * 1024 * 1024);  // 4MB max

std::vector<Grpc::Frame> frames;
auto status = decoder.decode(input, frames);

if (!status.ok()) {
    // Frame exceeded max size
    sendError(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
             "Message too large");
}
```

## Error Handling

### Oversized Frames

```cpp
// Set maximum frame size
decoder.setMaxFrameLength(max_message_size_);

// On decode
auto status = decoder.decode(input, frames);
if (!status.ok()) {
    // Frame too large - return error
    return Grpc::Status::WellKnownGrpcStatus::ResourceExhausted;
}
```

### Malformed Frames

```cpp
// Invalid flags, length, or incomplete data
if (!status.ok()) {
    return Grpc::Status::WellKnownGrpcStatus::Internal;
}
```

### Connection Errors

```cpp
// If connection resets mid-frame
if (decoder.hasBufferedData() && connection_closed) {
    // Incomplete frame - error
    return Grpc::Status::WellKnownGrpcStatus::Internal;
}
```

## Performance Considerations

### 1. Zero-Copy Decoding

The decoder doesn't copy payload data:

```cpp
void Decoder::frameData(uint8_t* data, uint64_t length) {
    // Add pointer to existing data (no copy)
    frame_.data_->add(data, length);
}
```

### 2. Incremental Parsing

Handles partial frames efficiently:

```cpp
// First chunk: 3 bytes (partial header)
decoder.decode(buffer, frames);  // No frames yet, buffers state

// Second chunk: 10 bytes (completes header + some data)
decoder.decode(buffer, frames);  // Still buffering

// Third chunk: Remaining data
decoder.decode(buffer, frames);  // Frame complete, added to output
```

### 3. Batch Decoding

Decode multiple frames in one call:

```cpp
// Buffer with 5 complete frames
std::vector<Grpc::Frame> frames;
decoder.decode(buffer, frames);
// frames.size() == 5
```

## Best Practices

### 1. Always Set Maximum Frame Size

```cpp
// Prevent DoS via huge messages
decoder.setMaxFrameLength(4 * 1024 * 1024);  // 4MB
```

### 2. Handle Partial Frames

```cpp
// Don't assume complete frames
if (decoder.hasBufferedData()) {
    // More data needed
}
```

### 3. Check for Compression

```cpp
for (auto& frame : frames) {
    if (frame.flags_ & Grpc::GRPC_FH_COMPRESSED) {
        // Decompress before parsing protobuf
        decompressFrame(frame);
    }
}
```

### 4. Use Helper Functions

```cpp
// Instead of manual encoding:
Buffer::InstancePtr Grpc::Common::serializeToGrpcFrame(
    const Protobuf::Message& message
) {
    auto buffer = std::make_unique<Buffer::OwnedImpl>();
    std::string serialized;
    message.SerializeToString(&serialized);
    buffer->add(serialized);

    Encoder encoder;
    encoder.prependFrameHeader(GRPC_FH_DEFAULT, *buffer);

    return buffer;
}
```

## Summary

The gRPC codec provides:
- **Simple wire format** - 5-byte header + payload
- **Incremental parsing** - Handles partial frames
- **Compression support** - Gzip compressed messages
- **Size limits** - Protection against oversized frames
- **Zero-copy** - Efficient data handling

**Key Point**: The codec is stateful for streaming - maintains parse state across multiple `decode()` calls to handle partial frames gracefully.

## Related

- **Async Client**: `async_client_impl.md` - Uses codec for message encoding/decoding
- **Common Utilities**: `common.md` - Helper functions for serialization
- **HTTP/2 Codec**: `/source/common/http/http2/codec_impl.h` - Underlying transport
