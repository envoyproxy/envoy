package passthrough

import (
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

func init() {
	http.RegisterHttpFilterFactoryAndConfigParser("passthrough", http.PassThroughFactory, http.NullParser)
}
