@_implementationOnly import EnvoyCxxSwiftInterop

// swiftlint:disable force_unwrapping

private typealias Logger = ((String) -> Void)?
private typealias BoxType = PointerBox<Logger>

enum EnvoyLoggerFactory {
  static func create(log: ((String) -> Void)?) -> envoy_logger {
    guard let log else {
      return envoy_logger(log: nil, release: nil, context: nil)
    }

    return envoy_logger(
      log: { stringData, context in
        guard let string = String.fromEnvoyData(stringData) else {
          return
        }

        let log = BoxType.unretained(from: context!)
        log?(string)
      },
      release: { context in
        BoxType.unmanaged(from: context!).release()
      },
      context: BoxType(value: log).retainedPointer()
    )
  }
}
