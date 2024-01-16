@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation
@_implementationOnly import std

// MARK: - String

extension String {
  static func fromCXX(_ cxxString: std.string) -> String {
    return String(cString: cxxString.c_str())
  }

  func toCXX() -> std.string {
    return self.utf8.reduce(into: std.string()) { result, codeUnit in
      result.push_back(Int8(codeUnit))
    }
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
}
