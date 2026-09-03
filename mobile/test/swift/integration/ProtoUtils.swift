// Minimal binary protobuf encoding helpers used by Swift integration tests.
// These replace text-format proto strings for compatibility with lite protos.

import Foundation

// MARK: - Low-level protobuf binary encoding

/// Encode an unsigned integer as a protobuf varint.
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

/// Encode a length-delimited field (wire type 2).
private func lenField(_ fieldNumber: Int, _ bytes: Data) -> Data {
  var data = Data()
  data.append(contentsOf: encodeVarint(UInt64(fieldNumber << 3 | 2)))
  data.append(contentsOf: encodeVarint(UInt64(bytes.count)))
  data.append(bytes)
  return data
}

/// Encode a string field.
private func stringField(_ fieldNumber: Int, _ s: String) -> Data {
  return lenField(fieldNumber, Data(s.utf8))
}

/// Encode a varint field (wire type 0).
private func varintField(_ fieldNumber: Int, _ value: UInt64) -> Data {
  var data = Data()
  data.append(contentsOf: encodeVarint(UInt64(fieldNumber << 3 | 0)))
  data.append(contentsOf: encodeVarint(value))
  return data
}

// MARK: - google.protobuf.Any builder

/// Build a binary-serialized `google.protobuf.Any` wrapping the given message bytes.
///
/// - parameter typeURL:      The fully-qualified type URL, e.g.
///                           "type.googleapis.com/envoymobile.extensions.filters.http.foo.Foo".
/// - parameter messageBytes: Binary-serialized inner message.
/// - returns: Binary-serialized `google.protobuf.Any`.
func makeAnyProto(typeURL: String, messageBytes: Data) -> Data {
  var data = Data()
  data.append(contentsOf: stringField(1, typeURL))  // field 1: type_url
  data.append(contentsOf: lenField(2, messageBytes))  // field 2: value
  return data
}

// MARK: - Filter-specific Any builders

/// Build a binary `Any` for `envoymobile.extensions.filters.http.test_logger.TestLogger`.
func makeTestLoggerAnyProto() -> Data {
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoymobile.extensions.filters.http.test_logger.TestLogger",
    messageBytes: Data()
  )
}

/// Build a binary `Any` for `envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker`
/// with the given key/value attribute map.
func makeTestEventTrackerAnyProto(attributes: [String: String]) -> Data {
  // TestEventTracker.attributes is map<string, string> = field 1.
  // A proto3 map is encoded as repeated message { key (field 1), value (field 2) }.
  var messageBytes = Data()
  for (key, value) in attributes {
    var entry = Data()
    entry.append(contentsOf: stringField(1, key))
    entry.append(contentsOf: stringField(2, value))
    messageBytes.append(contentsOf: lenField(1, entry))
  }
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoymobile.extensions.filters.http.test_event_tracker.TestEventTracker",
    messageBytes: messageBytes
  )
}

/// Build a binary `Any` for
/// `envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore`.
func makeTestKeyValueStoreAnyProto(
  kvStoreName: String, testKey: String, testValue: String
) -> Data {
  // TestKeyValueStore: kv_store_name = 1, test_key = 2, test_value = 3
  var messageBytes = Data()
  messageBytes.append(contentsOf: stringField(1, kvStoreName))
  messageBytes.append(contentsOf: stringField(2, testKey))
  messageBytes.append(contentsOf: stringField(3, testValue))
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoymobile.extensions.filters.http.test_kv_store.TestKeyValueStore",
    messageBytes: messageBytes
  )
}

/// Build a binary `Any` for
/// `envoymobile.extensions.filters.http.assertion.Assertion` with a body string match.
///
/// Matches `MatchPredicate.http_request_generic_body_match` (field 9) containing a
/// `GenericTextMatch.string_match` (field 1 of the `patterns` repeated field 2).
func makeAssertionBodyMatchAnyProto(stringMatch: String) -> Data {
  // GenericTextMatch { string_match (field 1) = stringMatch }
  let genTextMatch = stringField(1, stringMatch)
  // HttpGenericBodyMatch { patterns (field 2) = genTextMatch }
  let bodyMatch = lenField(2, genTextMatch)
  // MatchPredicate (oneof) { http_request_generic_body_match (field 9) = bodyMatch }
  let matchPredicate = lenField(9, bodyMatch)
  // Assertion { match_config (field 1) = matchPredicate }
  let assertion = lenField(1, matchPredicate)
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion",
    messageBytes: assertion
  )
}

/// Build a binary `Any` for
/// `envoymobile.extensions.filters.http.assertion.Assertion` with a trailers header match.
///
/// Matches `MatchPredicate.http_request_trailers_match` (field 6) with a
/// `HeaderMatcher { name, string_match { exact } }`.
func makeAssertionTrailersMatchAnyProto(headerName: String, exactMatch: String) -> Data {
  // StringMatcher { exact (field 1) = exactMatch }
  let stringMatcher = stringField(1, exactMatch)
  // HeaderMatcher { name (field 1) = headerName, string_match (field 13) = stringMatcher }
  var headerMatcher = Data()
  headerMatcher.append(contentsOf: stringField(1, headerName))
  headerMatcher.append(contentsOf: lenField(13, stringMatcher))
  // HttpHeadersMatch { headers (field 1) = headerMatcher }
  let headersMatch = lenField(1, headerMatcher)
  // MatchPredicate (oneof) { http_request_trailers_match (field 6) = headersMatch }
  let matchPredicate = lenField(6, headersMatch)
  // Assertion { match_config (field 1) = matchPredicate }
  let assertion = lenField(1, matchPredicate)
  return makeAnyProto(
    typeURL: "type.googleapis.com/envoymobile.extensions.filters.http.assertion.Assertion",
    messageBytes: assertion
  )
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
