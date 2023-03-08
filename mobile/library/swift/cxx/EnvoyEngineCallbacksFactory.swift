@_implementationOnly import EnvoyCxxSwiftInterop

// swiftlint:disable force_unwrapping

private typealias OnEngineRunning = (() -> Void)?
private typealias BoxType = PointerBox<OnEngineRunning>

enum EnvoyEngineCallbacksFactory {
  static func create(onEngineRunning: (() -> Void)?) -> envoy_engine_callbacks {
    envoy_engine_callbacks(
      on_engine_running: { context in
        let onEngineRunning = BoxType.unretained(from: context!)
        onEngineRunning?()
      },
      on_exit: { context in
        BoxType.unmanaged(from: context!).release()
      },
      context: BoxType(value: onEngineRunning).retainedMutablePointer()
    )
  }
}
