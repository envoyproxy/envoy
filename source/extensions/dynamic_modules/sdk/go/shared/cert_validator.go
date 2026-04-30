//go:generate mockgen -source=cert_validator.go -destination=mocks/mock_cert_validator.go -package=mocks
package shared

// Custom TLS certificate validator SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `cert_validator` module. A module that exposes a custom certificate
// validator implements CertValidatorConfigFactory and registers it from an init() function via
// sdk.RegisterCertValidatorConfigFactories. It integrates with Envoy's `custom_validator_config`
// in CertificateValidationContext, registered under the `envoy.tls.cert_validator` category.
//
// The module receives DER-encoded certificates during validation and returns a result indicating
// success or failure with optional TLS alert and error details. Asynchronous (Pending) validation
// is not supported.

// CertValidatorValidationStatus is the overall validation status returned by VerifyCertChain. It
// corresponds to envoy_dynamic_module_type_cert_validator_validation_status, mapping to
// ValidationResults::ValidationStatus in cert_validator.h.
type CertValidatorValidationStatus uint32

const (
	// CertValidatorValidationStatusSuccessful indicates the chain was validated.
	CertValidatorValidationStatusSuccessful CertValidatorValidationStatus = 0
	// CertValidatorValidationStatusFailed indicates the chain failed validation.
	CertValidatorValidationStatusFailed CertValidatorValidationStatus = 1
)

// CertValidatorClientValidationStatus is the detailed client validation status. It corresponds
// to Ssl::ClientValidationStatus in ssl_socket_extended_info.h.
type CertValidatorClientValidationStatus uint32

const (
	CertValidatorClientValidationStatusNotValidated        CertValidatorClientValidationStatus = 0
	CertValidatorClientValidationStatusNoClientCertificate CertValidatorClientValidationStatus = 1
	CertValidatorClientValidationStatusValidated           CertValidatorClientValidationStatus = 2
	CertValidatorClientValidationStatusFailed              CertValidatorClientValidationStatus = 3
)

// CertValidatorValidationResult is the value returned by CertValidator.VerifyCertChain. Use
// CertValidatorContext.SetErrorDetails (during VerifyCertChain) to attach a free-form error
// message visible in stream info / logs.
type CertValidatorValidationResult struct {
	// Status is the overall validation status (Successful or Failed).
	Status CertValidatorValidationStatus
	// DetailedStatus is the detailed client validation status.
	DetailedStatus CertValidatorClientValidationStatus
	// HasTLSAlert indicates whether TLSAlert is set.
	HasTLSAlert bool
	// TLSAlert is the TLS alert code to send on failure (e.g. SSL_AD_BAD_CERTIFICATE). Only
	// honored if HasTLSAlert is true.
	TLSAlert uint8
}

// CertValidator is the module-side certificate validator. Implementations must be safe for
// concurrent calls because VerifyCertChain runs on worker threads.
type CertValidator interface {
	// VerifyCertChain is called to verify a certificate chain during a TLS handshake.
	//
	// certs are DER-encoded certificate buffers; the leaf certificate is at index 0. The
	// buffers are owned by Envoy and valid only for the duration of the call. hostName is the
	// SNI host name; isServer is true on the server side (i.e., when validating client certs).
	//
	// To attach a human-readable error message, call ctx.SetErrorDetails before returning.
	VerifyCertChain(ctx CertValidatorContext, certs []UnsafeEnvoyBuffer, hostName string, isServer bool) CertValidatorValidationResult

	// GetSSLVerifyMode is called during SSL context initialization to get the SSL_VERIFY_*
	// flags to apply (e.g. SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT). Returning 0
	// means SSL_VERIFY_NONE. handshakerProvidesCertificates is true when the handshaker
	// provides certificates itself.
	GetSSLVerifyMode(handshakerProvidesCertificates bool) int32

	// UpdateDigest is called to contribute to the session-context hash. The module should
	// return bytes that uniquely identify its validation configuration so that configuration
	// changes invalidate existing TLS sessions.
	UpdateDigest() []byte

	// OnDestroy is called when the validator configuration is destroyed.
	OnDestroy()
}

// EmptyCertValidator is a no-op CertValidator. VerifyCertChain returns Successful/Validated;
// GetSSLVerifyMode returns 0; UpdateDigest returns nil.
type EmptyCertValidator struct{}

func (*EmptyCertValidator) VerifyCertChain(_ CertValidatorContext, _ []UnsafeEnvoyBuffer, _ string, _ bool) CertValidatorValidationResult {
	return CertValidatorValidationResult{
		Status:         CertValidatorValidationStatusSuccessful,
		DetailedStatus: CertValidatorClientValidationStatusValidated,
	}
}
func (*EmptyCertValidator) GetSSLVerifyMode(_ bool) int32 { return 0 }
func (*EmptyCertValidator) UpdateDigest() []byte          { return nil }
func (*EmptyCertValidator) OnDestroy()                    {}

// CertValidatorConfigFactory is the top-level factory the module registers via
// sdk.RegisterCertValidatorConfigFactories.
type CertValidatorConfigFactory interface {
	// Create parses unparsedConfig and returns a CertValidator. Returning a non-nil error
	// rejects the configuration.
	Create(name string, unparsedConfig []byte) (CertValidator, error)
}

// EmptyCertValidatorConfigFactory is a no-op CertValidatorConfigFactory.
type EmptyCertValidatorConfigFactory struct{}

func (*EmptyCertValidatorConfigFactory) Create(_ string, _ []byte) (CertValidator, error) {
	return &EmptyCertValidator{}, nil
}

// CertValidatorContext is the per-call handle passed to CertValidator.VerifyCertChain. It is
// valid only for the duration of that call; do not retain it. Methods on this handle are valid
// only inside VerifyCertChain.
type CertValidatorContext interface {
	// SetErrorDetails attaches an error message to the failed validation. The message is
	// recorded in the connection's stream info / logs. Envoy copies the buffer immediately;
	// the module does not need to keep it alive after the call returns.
	SetErrorDetails(errorDetails string)

	// SetFilterState stores a string value under key in the connection's filter state with
	// Connection lifespan. Returns false if no connection context is available or the key
	// already exists and is read-only.
	SetFilterState(key, value string) bool

	// GetFilterState retrieves a string value previously stored under key.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the
	// VerifyCertChain callback. Copy if you need to keep it.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)
}
