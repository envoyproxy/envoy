import Envoy
import UIKit

private let kCellID = "cell-id"
private let kRequestAuthority = "api.lyft.com"
private let kRequestPath = "/ping"
private let kRequestScheme = "https"
private let kFilteredHeaders =
  ["server", "filter-demo", "async-filter-demo", "x-envoy-upstream-service-time"]

final class ViewController: UITableViewController {
  private var results = [Result<Response, RequestError>]()
  private var timer: Foundation.Timer?
  private var streamClient: StreamClient?
  private var pulseClient: PulseClient?

  override func viewDidLoad() {
    super.viewDidLoad()

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addPlatformFilter(DemoFilter.init)
      .addPlatformFilter(BufferDemoFilter.init)
      .addPlatformFilter(AsyncDemoFilter.init)
      .h2ExtendKeepaliveTimeout(true)
      .enableInterfaceBinding(true)
      // swiftlint:disable:next line_length
      .addNativeFilter(name: "envoy.filters.http.buffer", typedConfig: "{\"@type\":\"type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer\",\"max_request_bytes\":5242880}")
      .setOnEngineRunning { NSLog("Envoy async internal setup completed") }
      .addStringAccessor(name: "demo-accessor", accessor: { return "PlatformString" })
      .setEventTracker { NSLog("Envoy event emitted: \($0)") }
      .build()
    self.streamClient = engine.streamClient()
    self.pulseClient = engine.pulseClient()

    NSLog("started Envoy, beginning requests...")
    self.startRequests()
  }

  deinit {
    self.timer?.invalidate()
  }

  // MARK: - Requests

  private func startRequests() {
    self.timer = .scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
      self?.performRequest()
      self?.recordStats()
    }
  }

  private func performRequest() {
    guard let streamClient = self.streamClient else {
      NSLog("failed to start request - Envoy is not running")
      return
    }

    NSLog("starting request to '\(kRequestPath)'")

    // Note: this request will use an h2 stream for the upstream request.
    // The Objective-C example uses http/1.1. This is done on purpose to test both paths in
    // end-to-end tests in CI.
    let headers = RequestHeadersBuilder(method: .get, scheme: kRequestScheme,
                                        authority: kRequestAuthority, path: kRequestPath)
      .addUpstreamHttpProtocol(.http2)
      .build()

    streamClient
      .newStreamPrototype()
      .setOnResponseHeaders { [weak self] headers, _, _ in
        let statusCode = headers.httpStatus.map(String.init) ?? "nil"
        let message = "received headers with status \(statusCode)"

        let headerMessage = headers.caseSensitiveHeaders()
          .filter { kFilteredHeaders.contains($0.key) }
          .map { "\($0.key): \($0.value.joined(separator: ", "))" }
          .joined(separator: "\n")

        NSLog(message)
        if let filterDemoValue = headers.value(forName: "filter-demo")?.first {
          NSLog("filter-demo: \(filterDemoValue)")
        }
        if let asyncFilterDemoValue = headers.value(forName: "async-filter-demo")?.first {
          NSLog("async-filter-demo: \(asyncFilterDemoValue)")
        }

        let response = Response(message: message,
                                headerMessage: headerMessage)
        self?.add(result: .success(response))
      }
      .setOnError { [weak self] error, _ in
        let message: String
        if let attemptCount = error.attemptCount {
          message = "failed within Envoy library after \(attemptCount) attempts: \(error.message)"
        } else {
          message = "failed within Envoy library: \(error.message)"
        }

        NSLog(message)
        self?.add(result: .failure(RequestError(message: message)))
      }
      .start()
      .sendHeaders(headers, endStream: true)
  }

  private func add(result: Result<Response, RequestError>) {
    self.results.insert(result, at: 0)
    self.tableView.reloadData()
  }

  private func recordStats() {
    guard let pulseClient = self.pulseClient else {
      NSLog("failed to send stats - Envoy is not running")
      return
    }

    let counter = pulseClient.counter(elements: ["foo", "bar", "counter"])
    counter.increment()
    counter.increment(count: 5)

    let gauge = pulseClient.gauge(elements: ["foo", "bar", "gauge"])
    gauge.set(value: 5)
    gauge.add(amount: 10)
    gauge.sub(amount: 1)

    let timer = pulseClient.timer(elements: ["foo", "bar", "timer"])
    let distribution = pulseClient.distribution(elements: ["foo", "bar", "distribution"])
    timer.recordDuration(durationMs: 15)
    distribution.recordValue(value: 15)
  }
  // MARK: - UITableView

  override func numberOfSections(in tableView: UITableView) -> Int {
    return 1
  }

  override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
    return self.results.count
  }

  override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath)
    -> UITableViewCell
  {
    let cell = tableView.dequeueReusableCell(withIdentifier: kCellID) ??
      UITableViewCell(style: .subtitle, reuseIdentifier: kCellID)

    let result = self.results[indexPath.row]
    switch result {
    case .success(let response):
      cell.textLabel?.text = response.message
      cell.detailTextLabel?.text = response.headerMessage

      cell.textLabel?.textColor = .black
      cell.detailTextLabel?.lineBreakMode = .byWordWrapping
      cell.detailTextLabel?.numberOfLines = 0
      cell.detailTextLabel?.textColor = .black
      cell.contentView.backgroundColor = .white
    case .failure(let error):
      cell.textLabel?.text = error.message
      cell.detailTextLabel?.text = nil

      cell.textLabel?.textColor = .white
      cell.detailTextLabel?.textColor = .white
      cell.contentView.backgroundColor = .red
    }

    return cell
  }
}
