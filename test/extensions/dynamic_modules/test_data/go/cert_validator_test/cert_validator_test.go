// Cert validator test module.
//
// Registers a "test" cert validator that always returns Successful. Loaded by the
// dynamic_modules_cert_validator_test under test/extensions/transport_sockets/tls/cert_validator/dynamic_modules.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterCertValidatorConfigFactories(map[string]shared.CertValidatorConfigFactory{
		"test": &noOpConfigFactory{},
	})
}

func main() {} //nolint:all

type noOpConfigFactory struct {
	shared.EmptyCertValidatorConfigFactory
}

func (noOpConfigFactory) Create(_ string, _ []byte) (shared.CertValidator, error) {
	return &noOpValidator{}, nil
}

type noOpValidator struct {
	shared.EmptyCertValidator
}
