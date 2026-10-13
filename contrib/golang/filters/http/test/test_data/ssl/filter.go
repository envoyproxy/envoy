package ssl

import (
	"fmt"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

type sslFilter struct {
	callbacks api.FilterCallbackHandler
}

func (f *sslFilter) DecodeHeaders(headers api.RequestHeaderMap, endStream bool) api.StatusType {
	streamInfo := f.callbacks.StreamInfo()

	// Check if connection is secured with SSL/TLS
	ssl := streamInfo.DownstreamSslConnection()
	if ssl == nil {
		f.callbacks.Log(api.Warn, "Connection is not SSL secured")
		headers.Set("x-ssl-tested", "no-ssl")
		return api.Continue
	}

	headers.Set("x-ssl-tested", "yes")

	// Test 1 & 2: Certificate presentation and validation
	presented := ssl.PeerCertificatePresented()
	validated := ssl.PeerCertificateValidated()
	headers.Set("x-cert-presented", fmt.Sprintf("%v", presented))
	headers.Set("x-cert-validated", fmt.Sprintf("%v", validated))

	if !presented {
		f.callbacks.Log(api.Error, "No client certificate presented")
		headers.Set("x-ssl-status", "no-cert")
		return api.Continue
	}

	if !validated {
		f.callbacks.Log(api.Error, "Client certificate validation failed")
		headers.Set("x-ssl-status", "invalid-cert")
		return api.Continue
	}

	// Test 3-12: Peer certificate string fields
	if digest := ssl.Sha256PeerCertificateDigest(); digest != "" {
		headers.Set("x-cert-digest", digest)
	}

	if serial := ssl.SerialNumberPeerCertificate(); serial != "" {
		headers.Set("x-cert-serial", serial)
	}

	if subject := ssl.SubjectPeerCertificate(); subject != "" {
		headers.Set("x-cert-subject", subject)
	}

	if issuer := ssl.IssuerPeerCertificate(); issuer != "" {
		headers.Set("x-cert-issuer", issuer)
	}

	if subjectLocal := ssl.SubjectLocalCertificate(); subjectLocal != "" {
		headers.Set("x-cert-subject-local", subjectLocal)
	}

	if urlEncodedPem := ssl.UrlEncodedPemEncodedPeerCertificate(); len(urlEncodedPem) > 0 {
		headers.Set("x-cert-pem-length", fmt.Sprintf("%d", len(urlEncodedPem)))
	}

	if urlEncodedChain := ssl.UrlEncodedPemEncodedPeerCertificateChain(); len(urlEncodedChain) > 0 {
		headers.Set("x-cert-chain-length", fmt.Sprintf("%d", len(urlEncodedChain)))
	}

	// Test 13-16: Subject Alternative Names (arrays)
	if dnsSansPeer := ssl.DnsSansPeerCertificate(); dnsSansPeer != nil {
		headers.Set("x-cert-dns-sans-peer-count", fmt.Sprintf("%d", len(dnsSansPeer)))
	}

	if dnsSansLocal := ssl.DnsSansLocalCertificate(); dnsSansLocal != nil {
		headers.Set("x-cert-dns-sans-local-count", fmt.Sprintf("%d", len(dnsSansLocal)))
	}

	if uriSansPeer := ssl.UriSanPeerCertificate(); uriSansPeer != nil {
		headers.Set("x-cert-uri-sans-peer-count", fmt.Sprintf("%d", len(uriSansPeer)))
	}

	if uriSansLocal := ssl.UriSanLocalCertificate(); uriSansLocal != nil {
		headers.Set("x-cert-uri-sans-local-count", fmt.Sprintf("%d", len(uriSansLocal)))
	}

	// Test 17-18: Certificate validity timestamps
	if validFrom, ok := ssl.ValidFromPeerCertificate(); ok {
		headers.Set("x-cert-valid-from", fmt.Sprintf("%d", validFrom))
	}

	if expiration, ok := ssl.ExpirationPeerCertificate(); ok {
		headers.Set("x-cert-expiration", fmt.Sprintf("%d", expiration))
	}

	// Test 19: TLS version
	tlsVersion := ssl.TlsVersion()
	headers.Set("x-tls-version", tlsVersion)

	// Test 20-21: Cipher suite
	if cipherSuite := ssl.CiphersuiteString(); cipherSuite != "" {
		headers.Set("x-cipher-suite", cipherSuite)
	}

	if cipherID, ok := ssl.CiphersuiteId(); ok {
		headers.Set("x-cipher-id", fmt.Sprintf("%d", cipherID))
	}

	// Test 22: Session ID
	if sessionID, ok := ssl.SessionId(); ok && len(sessionID) > 0 {
		headers.Set("x-session-id-length", fmt.Sprintf("%d", len(sessionID)))
	}

	f.callbacks.Log(api.Info, "SSL filter tested all 20+ SSL API methods")

	return api.Continue
}

func (f *sslFilter) EncodeHeaders(headers api.ResponseHeaderMap, endStream bool) api.StatusType {
	return api.Continue
}

func (f *sslFilter) DecodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *sslFilter) EncodeData(buffer api.BufferInstance, endStream bool) api.StatusType {
	return api.Continue
}

func (f *sslFilter) DecodeTrailers(trailers api.RequestTrailerMap) api.StatusType {
	return api.Continue
}

func (f *sslFilter) EncodeTrailers(trailers api.ResponseTrailerMap) api.StatusType {
	return api.Continue
}

func (f *sslFilter) OnDestroy(reason api.DestroyReason) {
}

func (f *sslFilter) OnStreamComplete() {
}

func (f *sslFilter) OnLog(reqHeaders api.RequestHeaderMap, reqTrailers api.RequestTrailerMap, respHeaders api.ResponseHeaderMap, respTrailers api.ResponseTrailerMap) {
}

func (f *sslFilter) OnLogDownstreamStart(reqHeaders api.RequestHeaderMap) {
}

func (f *sslFilter) OnLogDownstreamPeriodic(reqHeaders api.RequestHeaderMap, reqTrailers api.RequestTrailerMap, respHeaders api.ResponseHeaderMap, respTrailers api.ResponseTrailerMap) {
}

func filterFactory(cfg interface{}, callbacks api.FilterCallbackHandler) api.StreamFilter {
	return &sslFilter{
		callbacks: callbacks,
	}
}

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser("ssl", filterFactory, http.NullParser)
}
