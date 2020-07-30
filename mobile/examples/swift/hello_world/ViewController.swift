import Envoy
import UIKit

private let kCellID = "cell-id"
private let kRequestAuthority = "api.lyft.com"
private let kRequestPath = "/ping"
private let kRequestScheme = "https"

final class ViewController: UITableViewController {
  private var results = [Result<Response, RequestError>]()
  private var timer: Timer?
  private var client: StreamClient?

  override func viewDidLoad() {
    super.viewDidLoad()
    do {
      NSLog("starting Envoy...")
      self.client = try StreamClientBuilder()
        .addFilter(DemoFilter.init)
        .build()
    } catch let error {
      NSLog("starting Envoy failed: \(error)")
    }

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
    }
  }

  private func performRequest() {
    guard let client = self.client else {
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

    client
      .newStreamPrototype()
      .setOnResponseHeaders { [weak self] headers, _ in
        let statusCode = headers.httpStatus ?? -1
        var message = "received headers with status \(statusCode)"
        if let filterDemoValue = headers.value(forName: "filter-demo")?.first {
          message += " and filter-demo set to \(filterDemoValue)"
        }

        let response = Response(message: message,
                                serverHeader: headers.value(forName: "server")?.first ?? "")
        NSLog(message)
        self?.add(result: .success(response))
      }
      .setOnError { [weak self] error in
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
      cell.detailTextLabel?.text = "'server' header: \(response.serverHeader)"

      cell.textLabel?.textColor = .black
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
