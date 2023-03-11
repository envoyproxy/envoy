@_implementationOnly import EnvoyCxxSwiftInterop

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
        let mutablePointer = UnsafeMutableRawPointer(mutating: context!)
        let track = BoxType.unretained(from: mutablePointer)
        track?(.fromEnvoyMap(map))
      },
      context: BoxType(value: track).retainedPointer()
    )
  }
}
