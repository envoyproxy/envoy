package go_build_test

import (
	"testing"

	_ "github.com/envoyproxy/data-plane-api/api/envoy/api/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/api/v2/auth"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/config/bootstrap/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/service/accesslog/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/service/discovery/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/service/metrics/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/service/ratelimit/v2"
	_ "github.com/envoyproxy/data-plane-api/api/envoy/service/trace/v2"
)

func TestNoop(t *testing.T) {
	// Noop test that verifies the successful importation of Envoy V2 API protos
}
