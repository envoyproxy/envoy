package secrets

import (
	"net/http"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type filter struct {
	api.PassThroughStreamFilter
	callbacks     api.FilterCallbackHandler
	secretManager api.SecretManager
}

func (f *filter) DecodeHeaders(header api.RequestHeaderMap, _ bool) api.StatusType {
	secretKey, _ := header.Get("secret_key")
	secret, ok := f.secretManager.GetGenericSecret(secretKey)
	statusCode := http.StatusOK
	if !ok {
		statusCode = http.StatusNotFound
	}
	f.callbacks.DecoderFilterCallbacks().SendLocalReply(statusCode, secret, nil, 0, "")
	return api.LocalReply
}
