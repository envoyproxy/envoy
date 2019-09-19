import Envoy
import UIKit

private let kCellID = "cell-id"
private let kRequestAuthority = "s3.amazonaws.com"
private let kRequestPath = "/api.lyft.com/static/demo/hello_world.txt"
private let kRequestScheme = "http"

final class ViewController: UITableViewController {
  private var requestCount = 0
  private var results = [Result<Response, RequestError>]()
  private var timer: Timer?
  private var envoy: EnvoyClient?

  override func viewDidLoad() {
    super.viewDidLoad()
    do {
      NSLog("Starting Envoy...")
      self.envoy = try EnvoyClientBuilder(domain: kRequestAuthority)
        .build()
    } catch let error {
      NSLog("Starting Envoy failed: \(error)")
    }

    NSLog("Started Envoy, beginning requests...")
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
    guard let envoy = self.envoy else {
      NSLog("Failed to start request - Envoy is not running")
      return
    }

    self.requestCount += 1
    NSLog("Starting request to '\(kRequestPath)'")

    let requestID = self.requestCount
    let request = RequestBuilder(method: .get, scheme: kRequestScheme,
                                 authority: kRequestAuthority,
                                 path: kRequestPath).build()
    let handler = ResponseHandler()
      .onHeaders { [weak self] headers, statusCode, _ in
        NSLog("Response status (\(requestID)): \(statusCode)\n\(headers)")
        self?.add(result: .success(Response(id: requestID, body: "Status: \(statusCode)",
                                            serverHeader: headers["server"]?.first ?? "")))
      }
      .onData { data, _ in
        NSLog("Response data (\(requestID)): \(data.count) bytes")
      }
      .onError { [weak self] error in
        let message = "failed within Envoy library: \(error.message)"
        NSLog("Error (\(requestID)): \(message)")
        self?.add(result: .failure(RequestError(id: requestID,
                                                message: message)))
      }

    envoy.send(request, body: nil, handler: handler)
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
      cell.textLabel?.text = "[\(response.id)] \(response.body)"
      cell.detailTextLabel?.text = "'server' header: \(response.serverHeader)"

      cell.textLabel?.textColor = .black
      cell.detailTextLabel?.textColor = .black
      cell.contentView.backgroundColor = .white

    case .failure(let error):
      cell.textLabel?.text = "[\(error.id)]"
      cell.detailTextLabel?.text = error.message

      cell.textLabel?.textColor = .white
      cell.detailTextLabel?.textColor = .white
      cell.contentView.backgroundColor = .red
    }

    return cell
  }
}
