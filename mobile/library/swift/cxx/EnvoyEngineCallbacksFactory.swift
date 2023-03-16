@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation

// swiftlint:disable force_unwrapping

private typealias OnEngineRunning = (() -> Void)?
private typealias BoxType = PointerBox<OnEngineRunning>

enum EnvoyEngineCallbacksFactory {
  static func create(onEngineRunning: (() -> Void)?) -> envoy_engine_callbacks {
    envoy_engine_callbacks(
      on_engine_running: { context in
        // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
        // block is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
        autoreleasepool {
          let onEngineRunning = BoxType.unretained(from: context!)
          onEngineRunning?()
        }
      },
      on_exit: { context in
        BoxType.unmanaged(from: context!).release()
      },
      context: BoxType(value: onEngineRunning).retainedMutablePointer()
    )
  }
}
