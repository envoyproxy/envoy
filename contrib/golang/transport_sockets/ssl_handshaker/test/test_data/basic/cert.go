package basic

import (
	"sync"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/transport_sockets/ssl_handshaker/source/go/pkg/ssl_handshaker"
)

type basic struct {
}

var cache = sync.Map{}

func (b *basic) TrySelectCertSync(serverName string) (certName string, done bool) {
	if v, ok := cache.Load(serverName); ok {
		return v.(string), true
	}
	return "", false
}

func genCertName(serverName string) string {
	// do anything async, e.g. call remote server to generate cert & push to control plane
	return "cert_" + serverName
}

func (b *basic) SelectCertAsync(serverName string) string {
	name := genCertName(serverName)
	cache.Store(serverName, name)
	return name
}

func factory(conn *ssl_handshaker.DownstreamTlsConn) api.DownstreamTlsCertSelector {
	return &basic{}
}

ssl_handshaker.RegisterDownstreamTlsCertSelectorFactory(factory)
