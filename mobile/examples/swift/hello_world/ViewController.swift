import UIKit

private let kCellID = "cell-id"
private let kURL = URL(string: "http://0.0.0.0:9001/api.lyft.com/static/demo/hello_world.txt")!

final class ViewController: UITableViewController {
    private var responses = [Response]()
    private var timer: Timer?

    override func viewDidLoad() {
        super.viewDidLoad()
        self.startRequests()
    }

    deinit {
        self.timer?.invalidate()
    }

    // MARK: - Requests

    private func startRequests() {
        // Note that the first delay will give Envoy time to start up.
        self.timer = .scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.performRequest()
        }
    }

    private func performRequest() {
        let request = URLRequest(url: kURL)
        NSLog("Starting request to '\(kURL.path)")
        let task = URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            if let response = response as? HTTPURLResponse, response.statusCode == 200, let data = data {
                self?.handle(response: response, with: data)
            } else if let error = error {
                NSLog("Received error: \(error)")
            } else {
                NSLog("Failed to receive data from the server")
            }
        }

        task.resume()
    }

    private func handle(response: HTTPURLResponse, with data: Data) {
        guard let body = String(data: data, encoding: .utf8) else {
            return NSLog("Failed to deserialize response string")
        }

        let untypedHeaders = response.allHeaderFields
        let headers = Dictionary(uniqueKeysWithValues: untypedHeaders.map { header -> (String, String) in
            return (header.key as? String ?? String(describing: header.key), "\(header.value)")
        })

        NSLog("Response:\n\(data.count) bytes\n\(body)\n\(headers)")

        let value = Response(body: body, serverHeader: headers["Server"] ?? "")
        DispatchQueue.main.async {
            self.responses.append(value)
            self.tableView.reloadData()
        }
    }

    // MARK: - UITableView

    override func numberOfSections(in tableView: UITableView) -> Int {
        return 1
    }

    override func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return self.responses.count
    }

    override func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: kCellID) ??
            UITableViewCell(style: .subtitle, reuseIdentifier: kCellID)

        let response = self.responses[indexPath.row]
        cell.textLabel?.text = "Response: \(response.body)"
        cell.detailTextLabel?.text = "'Server' header: \(response.serverHeader)"
        return cell
    }
}
