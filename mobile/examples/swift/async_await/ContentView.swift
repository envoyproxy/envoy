import Envoy
import Foundation
import SwiftUI

// MARK: - Content View

private struct ContentRow: Identifiable {
    let id = UUID()
    let content: String
}

private extension EngineBuilder {
    static let demoEngine = EngineBuilder()
        .addLogLevel(.debug)
        .build()
}

struct ContentView: View {
    @State private var rows: [ContentRow] = []
    private let start = Date()

    var body: some View {
        List {
            ForEach(rows) { row in
                Text(row.content)
            }
        }
        .task {
            writeLog("Generating data")
            let data = Data.ones(byteCount: 5 * 1_024 * 1_024)

            writeLog("Initializing Envoy Engine")
            let streamClient = EngineBuilder.demoEngine
                .streamClient()

            writeLog("Dispatching requests")
            for index in 0..<3 {
                await streamClient.postToHTTPBin(data: data, logger: { log in
                    Task.detached {
                        await writeLog("[\(index)] \(log)")
                    }
                })
            }
        }
    }

    @MainActor
    private func writeLog(_ log: String) {
        let logWithTiming = String(format: "\(log) (%.2f)", -start.timeIntervalSinceNow)
        NSLog(logWithTiming)
        withAnimation {
            rows.insert(ContentRow(content: logWithTiming), at: 0)
        }
    }
}

// MARK: - Post Data To HTTP Bin

private extension StreamClient {
    func postToHTTPBin(data: Data, logger: @escaping (String) -> Void) async {
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            DispatchQueue.networking.async {
                self.postToHTTPBin(data: data, logger: logger)
                continuation.resume()
            }
        }
    }

    func postToHTTPBin(data: Data, logger: @escaping (String) -> Void) {
        logger("Initiating upload")

        let stream = self
            .newStreamPrototype()
            .setOnResponseHeaders { headers, _, _ in
                let allHeaders = headers.allHeaders()

                if allHeaders[":status"]?.first == "200",
                   let contentLengthValue = allHeaders["Content-Length"],
                   let firstContentLength = contentLengthValue.first,
                   let contentLengthInt = Int64(firstContentLength)
                {
                    let formattedByteCount = ByteCountFormatter()
                        .string(fromByteCount: contentLengthInt)
                    logger("Uploaded \(formattedByteCount) of data")
                    return
                }

                let headerMessage = allHeaders
                    .map { "\($0.key): \($0.value.joined(separator: ", "))" }
                    .joined(separator: "\n")

                logger("Upload failed.\nHeaders: \(headerMessage)")
            }
            .start(queue: .networking)

        let headers = RequestHeadersBuilder(method: .post, scheme: "https",
                                            authority: "httpbin.org", path: "/post")
            .build()
        stream.sendHeaders(headers, endStream: false)
        stream.close(data: data)
        logger("Finished scheduling upload")
    }
}

// MARK: - Data Utilities

private extension Data {
    static func ones(byteCount: Int) -> Data {
        return Data(bytes: Array(repeating: UInt8.max, count: byteCount), count: byteCount)
    }
}

// MARK: - Networking Concurrent Queue

private extension DispatchQueue {
    static let networking = DispatchQueue(label: "com.lyft.envoymobile.networking",
                                          qos: .userInitiated, attributes: .concurrent)
}
