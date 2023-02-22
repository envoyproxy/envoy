// This is a NOOP plugin for the purpose of configuration testing.

package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/registry"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

func init() {
	registry.RegisterHttpFilterConfigFactory("", http.PassThroughFactory)
}

func main() {
}
