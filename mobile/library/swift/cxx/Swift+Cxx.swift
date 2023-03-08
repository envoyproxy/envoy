@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation
@_implementationOnly import std

// MARK: - String

extension String {
  func toCXX() -> std.string {
    return self.utf8.reduce(into: std.string()) { result, codeUnit in
      result.push_back(Int8(codeUnit))
    }
  }

  func toEnvoyData() -> envoy_data {
    return self.data(using: .utf8).toEnvoyData()
  }

  static func fromEnvoyData(_ data: envoy_data) -> String? {
    let data = Data(bytes: data.bytes, count: data.length)
    return String(data: data, encoding: .utf8)
  }
}

// MARK: - Data

extension Data {
  init(_ data: envoy_data) {
    self.init(bytes: data.bytes, count: data.length)
    release_envoy_data(data)
  }

  func toEnvoyData() -> envoy_data {
    let bytes = UnsafeMutableRawBufferPointer.allocate(byteCount: self.count, alignment: 0)
    self.copyBytes(to: bytes)
    // swiftlint:disable:next force_unwrapping
    let typedBytes = bytes.baseAddress!.assumingMemoryBound(to: UInt8.self)
    return envoy_data(length: count, bytes: typedBytes, release: free, context: typedBytes)
  }
}

func toEnvoyDataPtr(_ data: Data?) -> UnsafeMutablePointer<envoy_data>? {
  guard let data else {
    return nil
  }

  let ptr = UnsafeMutablePointer<envoy_data>.allocate(capacity: 1)
  var envoyData = data.toEnvoyData()
  ptr.assign(from: &envoyData, count: 1)
  return ptr
}

// MARK: - Optional

extension Optional<Data> {
  func toEnvoyData() -> envoy_data {
    self?.toEnvoyData() ?? envoy_nodata
  }
}

// MARK: - Collections

extension Array<String> {
  func toCXX() -> Envoy.CxxSwift.StringVector {
    return self.reduce(into: Envoy.CxxSwift.StringVector()) { result, string in
      var cxxString = string.toCXX()
      result.push_back(&cxxString)
    }
  }
}

extension Dictionary<String, String> {
  func toCXX() -> Envoy.CxxSwift.StringMap {
    return self.reduce(into: Envoy.CxxSwift.StringMap()) { result, keyValue in
      let (key, value) = keyValue
      Envoy.CxxSwift.string_map_set(&result, key.toCXX(), value.toCXX())
    }
  }

  func toEnvoyMap() -> envoy_map {
    let entries = UnsafeMutablePointer<envoy_map_entry>.allocate(capacity: self.count)
    var envoyMap = envoy_map(length: 0, entries: entries)
    for (key, value) in self {
      let entry = envoy_map_entry(
        key: key.toEnvoyData(),
        value: value.toEnvoyData()
      )
      entries[Int(envoyMap.length)] = entry
      envoyMap.length += 1
    }
    return envoyMap
  }

  static func fromEnvoyMap(_ map: envoy_map) -> Self {
    (0..<Int(map.length)).reduce(into: [:]) { result, i in
      let entry = map.entries[i]
      if let key = String.fromEnvoyData(entry.key), let value = String.fromEnvoyData(entry.value) {
        result[key] = value
      }
    }
  }
}
