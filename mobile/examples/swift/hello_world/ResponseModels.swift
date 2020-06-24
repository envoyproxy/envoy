/// Represents a response from the server.
struct Response {
  let message: String
  let serverHeader: String
}

/// Error that was encountered when executing a request.
struct RequestError: Error {
  let message: String
}
