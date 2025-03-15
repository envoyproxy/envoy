package secrets

import (
	"net/http"
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *filter) DecodeHeaders(_ api.RequestHeaderMap, _ bool) api.StatusType {
	if f.config.path == "/config" {
		f.callbacks.DecoderFilterCallbacks().SendLocalReply(200, f.config.secretValue, nil, 0, "")
		return api.LocalReply
	}
	fun := func() {
		secretManager := f.callbacks.SecretManager()
		if strings.Contains(f.config.path, "config") {
			secretManager = f.config.secretManager
		}
		secret, ok := secretManager.GetGenericSecret(f.config.secretKey)
		statusCode := http.StatusOK
		if !ok {
			statusCode = http.StatusNotFound
		}
		f.callbacks.DecoderFilterCallbacks().SendLocalReply(statusCode, secret, nil, 0, "")
	}
	if strings.Contains(f.config.path, "async") {
		go fun()
		return api.Running
	}
	fun()
	return api.LocalReply
}
