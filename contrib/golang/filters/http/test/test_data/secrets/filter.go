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
	fun := func() {
		secretManager := f.callbacks.SecretManager()
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
