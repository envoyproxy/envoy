@testable import Envoy
@_implementationOnly import EnvoyCxxSwiftInterop
import XCTest

final class EnvoyEngineCallbacksFactoryTests: XCTestCase {
  func testBasic() {
    var didCall = false
    let callbacks = EnvoyEngineCallbacksFactory.create(onEngineRunning: {
      didCall = true
    })
    XCTAssertFalse(didCall)
    callbacks.on_engine_running(callbacks.context)
    XCTAssertTrue(didCall)
    callbacks.on_exit(callbacks.context)
  }
}
