/// Represents a response from the server.
struct Response {
  let message: String
  let headerMessage: String
}

/// Error that was encountered when executing a request.
struct RequestError: Error {
  let message: String
}
