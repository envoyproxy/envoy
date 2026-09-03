import Foundation

// Minimal binary protobuf encoding helper used by the experimental app.
// Replaces the text-format proto string for compatibility with lite protos.

private func encodeVarint(_ value: UInt64) -> Data {
  var data = Data()
  var v = value
  while v > 127 {
    data.append(UInt8(v & 0x7F | 0x80))
    v >>= 7
  }
  data.append(UInt8(v))
  return data
}

private func lenField(_ fieldNumber: Int, _ bytes: Data) -> Data {
  var data = Data()
  data.append(contentsOf: encodeVarint(UInt64(fieldNumber << 3 | 2)))
  data.append(contentsOf: encodeVarint(UInt64(bytes.count)))
  data.append(bytes)
  return data
}

private func stringField(_ fieldNumber: Int, _ s: String) -> Data {
  return lenField(fieldNumber, Data(s.utf8))
}

private func varintField(_ fieldNumber: Int, _ value: UInt64) -> Data {
  var data = Data()
  data.append(contentsOf: encodeVarint(UInt64(fieldNumber << 3 | 0)))
  data.append(contentsOf: encodeVarint(value))
  return data
}

private func makeAnyProto(typeURL: String, messageBytes: Data) -> Data {
  var data = Data()
  data.append(contentsOf: stringField(1, typeURL))
  data.append(contentsOf: lenField(2, messageBytes))
  return data
}

/// Build a binary `Any` for `envoy.extensions.filters.http.buffer.v3.Buffer`
/// with `max_request_bytes` set to the given value.
func makeBufferAnyProto(maxRequestBytes: UInt32) -> Data {
  // google.protobuf.UInt32Value { value (field 1) = maxRequestBytes }
  let uint32Value = varintField(1, UInt64(maxRequestBytes))
  // Buffer { max_request_bytes (field 1) = uint32Value }
  let buffer = lenField(1, uint32Value)
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer",
    messageBytes: buffer
  )
}
