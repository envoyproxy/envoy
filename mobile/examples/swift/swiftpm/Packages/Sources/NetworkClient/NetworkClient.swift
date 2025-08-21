import Envoy
import Foundation

// MARK: - Network Client

public enum NetworkClient {
    public static func getCatFact() async throws -> String {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.networking.async {
                let stream = EngineBuilder.demoEngine
                    .streamClient()
                    .newStreamPrototype()
                    .setOnResponseData(closure: { body, _, _ in
                        do {
                            let fact = try JSONDecoder().decode(FactResponse.self, from: body)
                            continuation.resume(returning: fact.fact)
                        } catch {
                            continuation.resume(throwing: FactError.parse)
                        }
                    })
                    .setOnError(closure: { error, _ in
                        continuation.resume(throwing: FactError.network(error))
                    })
                    .start(queue: .networking)

                let headers = RequestHeadersBuilder(
                    method: .get, scheme: "https", authority: "catfact.ninja", path: "/fact"
                ).build()
                stream.sendHeaders(headers, endStream: true)
            }
        }
    }
}

// MARK: - Envoy Engine

private extension EngineBuilder {
    static let demoEngine = EngineBuilder()
        .build()
}

// MARK: - Networking Concurrent Queue

private extension DispatchQueue {
    static let networking = DispatchQueue(label: "io.envoyproxy.envoymobile.networking",
                                          qos: .userInitiated, attributes: .concurrent)
}

// MARK: - Models

private struct FactResponse: Codable {
    let fact: String
}

private enum FactError: LocalizedError {
    case parse
    case network(Error)

    var localizedDescription: String {
        switch self {
        case .parse:
            return "Could not parse fact response"
        case .network(let error):
            return """
                Could not fetch fact
                \(error.localizedDescription)
                """
        }
    }
}
