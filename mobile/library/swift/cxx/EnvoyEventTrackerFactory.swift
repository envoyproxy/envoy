@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation

// swiftlint:disable force_unwrapping

private typealias Tracker = (([String: String]) -> Void)?
private typealias BoxType = PointerBox<Tracker>

enum EnvoyEventTrackerFactory {
  static func create(track: (([String: String]) -> Void)?) -> envoy_event_tracker {
    guard let track else {
      return envoy_event_tracker(track: nil, context: nil)
    }

    // TODO(jpsim): Add `release` field
    return envoy_event_tracker(
      track: { map, context in
        // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
        // block is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
        autoreleasepool {
          let mutablePointer = UnsafeMutableRawPointer(mutating: context!)
          let track = BoxType.unretained(from: mutablePointer)
          track?(.fromEnvoyMap(map))
        }
      },
      context: BoxType(value: track).retainedPointer()
    )
  }
}
