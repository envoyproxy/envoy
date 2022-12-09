/// The container that manages the underlying headers map.
/// It maintains the original casing of passed header names.
/// It treats headers names as case-insensitive for the purpose
/// of header lookups and header name conflict resolutions.
struct HeadersContainer: Equatable {
  private var headers: [String: Header]

  /// Represents a header name together with all of its values.
  /// It preserves the original casing of the header name.
  struct Header: Equatable {
    private(set) var name: String
    private(set) var value: [String]

    init(name: String, value: [String] = []) {
      self.name = name
      self.value = value
    }

    mutating func addValues(_ values: [String]) {
      self.value.append(contentsOf: values)
    }

    mutating func addValue(_ value: String) {
      self.value.append(value)
    }
  }

  /// Initialize a new instance of the receiver using the provided headers map.
  ///
  /// - parameter headers: The headers map.
  init(headers: [String: [String]]) {
    var underlyingHeaders = [String: Header]()
    for (name, value) in headers {
      let lowercasedName = name.lowercased()
      /// Dictionaries are unordered collections. Process headers with names
      /// that are the same when lowercased in an alphabetical order to avoid a situation
      /// in which the result of the initialization is non-derministic i.e., we want
      /// ["A": ["1"], "a": ["2"]] headers to be always converted to ["A": ["1", "2"]] and
      /// never to "a": ["2", "1"].
      ///
      /// If a given header name already exists in the processed headers map, check
      /// if the currently processed header name is before the existing header name as
      /// determined by an alphabetical order.
      guard let existingHeader = underlyingHeaders[lowercasedName] else {
        underlyingHeaders[lowercasedName] = Header(name: name, value: value)
        continue
      }

      if existingHeader.name > name {
        underlyingHeaders[lowercasedName] =
          Header(name: name, value: value + existingHeader.value)
      } else {
        underlyingHeaders[lowercasedName]?.addValues(value)
      }
    }
    self.headers = underlyingHeaders
  }

  /// Initialize an empty headers container.
  init() {
    self.headers = [:]
  }

  /// Add a value to a header with a given name.
  ///
  /// - parameter name:  The name of the header. For the purpose of headers lookup
  ///                    and header name conflict resolution, the name of the header
  ///                    is considered to be case-insensitive.
  /// - parameter value: The value to add.
  mutating func add(name: String, value: String) {
      self.headers[name.lowercased(), default: Header(name: name)].addValue(value)
  }

  /// Set the value of a given header.
  ///
  /// - parameter name:  The name of the header.
  /// - parameter value: The value to set the header value to.
  mutating func set(name: String, value: [String]) {
    self.headers[name.lowercased()] = Header(name: name, value: value)
  }

  /// Remove a given header.
  ///
  /// - parameter name: The name of the header to remove.
  mutating func remove(name: String) {
    self.headers[name.lowercased()] = nil
  }

  /// Get the value for the provided header name.
  ///
  /// - parameter name: The case-insensitive header name for which to
  ///                   get the current value.
  ///
  /// - returns: The value associated with a given header.
  func value(forName name: String) -> [String]? {
    return self.headers[name.lowercased()]?.value
  }

  /// Accessor for all underlying case-sensitive headers. When possible,
  /// use case-insensitive accessors instead.
  ///
  /// - returns: The underlying headers.
  func caseSensitiveHeaders() -> [String: [String]] {
    return Dictionary(uniqueKeysWithValues: self.headers.map { _, value in
      return (value.name, value.value)
    })
  }
}

extension HeadersContainer: CustomStringConvertible {
  var description: String {
    return self.headers.description
  }
}
