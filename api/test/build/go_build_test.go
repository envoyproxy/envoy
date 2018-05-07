package go_build_test

import (
	"testing"

	_ "github.com/envoyproxy/data-plane-api/api/ads"
	_ "github.com/envoyproxy/data-plane-api/api/bootstrap"
	_ "github.com/envoyproxy/data-plane-api/api/cds"
	_ "github.com/envoyproxy/data-plane-api/api/cert"
	_ "github.com/envoyproxy/data-plane-api/api/eds"
	_ "github.com/envoyproxy/data-plane-api/api/hds"
	_ "github.com/envoyproxy/data-plane-api/api/lds"
	_ "github.com/envoyproxy/data-plane-api/api/rds"
	_ "github.com/envoyproxy/data-plane-api/api/rls"
	_ "github.com/envoyproxy/data-plane-api/api/sds"
)

func TestNoop(t *testing.T) {
	// Noop test that verifies the successful importation of Envoy V2 API protos
}
