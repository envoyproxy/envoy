package istio.authz

default allow = false

allow = response {
  input.attributes.request.http.method == "GET"
  response := {
    "allowed": true,
    "headers": {"x-current-user": "OPA"}
  }
}
