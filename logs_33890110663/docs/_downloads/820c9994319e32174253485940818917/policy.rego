package envoy.authz

import input.attributes.request.http as http_request
import rego.v1

default allow := false

allow := response if {
	http_request.method == "GET"
	response := {
		"allowed": true,
		"headers": {"x-current-user": "OPA"},
	}
}
