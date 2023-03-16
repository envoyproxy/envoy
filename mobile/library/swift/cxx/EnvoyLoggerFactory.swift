@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation

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
        // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
        // block is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
        autoreleasepool {
          guard let string = String.fromEnvoyData(stringData) else {
            return
          }

          let log = BoxType.unretained(from: context!)
          log?(string)
        }
      },
      release: { context in
        BoxType.unmanaged(from: context!).release()
      },
      context: BoxType(value: log).retainedPointer()
    )
  }
}
