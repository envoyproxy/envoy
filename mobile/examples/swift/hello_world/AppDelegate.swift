import Envoy
import UIKit

private enum ConfigLoadError: Swift.Error {
  case noFileAtPath
}

@UIApplicationMain
final class AppDelegate: UIResponder, UIApplicationDelegate {
  private var envoy: Envoy!
  var window: UIWindow?

  func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool
  {
    NSLog("Loading config")
    let envoyConfig = try! self.loadEnvoyConfig()
    NSLog("Loaded config:\n\(envoyConfig)")
    self.envoy = Envoy(config: envoyConfig)

    let window = UIWindow(frame: UIScreen.main.bounds)
    window.rootViewController = ViewController()
    window.makeKeyAndVisible()
    self.window = window
    NSLog("Finished launching!")

    return true
  }

  private func loadEnvoyConfig() throws -> String {
    guard let configFile = Bundle.main.path(forResource: "config", ofType: "yaml") else {
      throw ConfigLoadError.noFileAtPath
    }

    return try String(contentsOfFile: configFile, encoding: .utf8)
  }
}
