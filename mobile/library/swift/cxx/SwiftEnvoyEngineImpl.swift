@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine
import Foundation
#if canImport(UIKit)
@_implementationOnly import UIKit
#endif

final class SwiftEnvoyEngineImpl: EnvoyEngine {
  private let handle: envoy_engine_t
  private let enableNetworkMonitor: Bool
#if canImport(EnvoyCxxSwiftInterop)
  private lazy var networkMonitor = NetworkMonitor()
#endif
  private var notificationObserver: NSObjectProtocol?

  init(runningCallback onEngineRunning: (() -> Void)?, logger: ((String) -> Void)?,
       eventTracker: (([String: String]) -> Void)?, networkMonitoringMode: Int32)
  {
    self.handle = init_engine(
      EnvoyEngineCallbacksFactory.create(onEngineRunning: onEngineRunning),
      EnvoyLoggerFactory.create(log: logger),
      EnvoyEventTrackerFactory.create(track: eventTracker)
    )

    let networkMonitoringMode = NetworkMonitoringMode(rawValue: Int(networkMonitoringMode))
    switch networkMonitoringMode {
    case .disabled, nil:
      self.enableNetworkMonitor = false
    case .reachability:
      fatalError("SwiftEnvoyEngineImpl does not support the reachability network monitoring mode")
    case .pathMonitor:
      self.enableNetworkMonitor = true
    }
  }

  func run(withYAML yaml: String, config: EnvoyConfiguration, logLevel: String) -> Int32 {
    self.commonRun(config: config)
    return Int32(run_engine(self.handle, yaml, logLevel, "").rawValue)
  }

  func run(withConfig config: EnvoyConfiguration, bootstrap: Bootstrap, logLevel: LogLevel) {
    self.commonRun(config: config)
    Envoy.CxxSwift.run(bootstrap.ptr, logLevel.toCXX(), self.handle)
  }

  // swiftlint:disable:next unavailable_function - Required for protocol conformance
  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    fatalError("Unimplemented. Use `run(withConfig:bootstrap:logLevel:)` instead.")
  }

  private func commonRun(config: EnvoyConfiguration) {
    for filterFactory in config.httpPlatformFilterFactories {
      filterFactory.register()
    }

    for (name, accessor) in config.stringAccessors {
      accessor.register(withName: name)
    }

    for (name, store) in config.keyValueStores {
      (store as! KeyValueStore).register(withName: name)
    }

    register_apple_platform_cert_verifier()

    let engineHandle = self.handle
#if canImport(UIKit)
    self.notificationObserver = NotificationCenter.default.addObserver(
      forName: UIApplication.willTerminateNotification,
      object: nil,
      queue: nil
    ) { [weak self] notification in
      print("[Envoy \(engineHandle)] terminating engine (\(notification.name)")
      self?.terminate()
    }
#endif

    if self.enableNetworkMonitor {
      self.networkMonitor.start(engineHandle: self.handle)
    }
  }

  func startStream(with callbacks: EnvoyHTTPCallbacks, explicitFlowControl: Bool) -> EnvoyHTTPStream
  {
    SwiftEnvoyHTTPStreamImpl(
      handle: init_stream(self.handle),
      engine: self.handle,
      callbacks: callbacks,
      explicitFlowControl: explicitFlowControl
    )
  }

  func recordCounterInc(_ elements: String, tags: [String: String], count: UInt) -> Int32 {
    return elements.withCString { cString in
      return Int32(
        record_counter_inc(
          self.handle, cString, tags.toEnvoyMap(), UInt64(count)
        ).rawValue
      )
    }
  }

  func flushStats() {
    flush_stats(self.handle)
  }

  func dumpStats() -> String {
    var data = envoy_data()
    let status = dump_stats(self.handle, &data)
    if status != ENVOY_SUCCESS {
      return ""
    }

    return .fromEnvoyData(data) ?? ""
  }

  func terminate() {
    terminate_engine(self.handle, /* release */ false)
  }

  func resetConnectivityState() {
    reset_connectivity_state(self.handle)
  }

  deinit {
    self.terminate()
    if let notificationObserver = self.notificationObserver {
      NotificationCenter.default.removeObserver(notificationObserver)
    }
  }
}
