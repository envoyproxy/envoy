// This is a NOOP plugin for the purpose of configuration testing.

package main

import "github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/http"

func init() {
	http.RegisterHttpFilterConfigFactory("", http.PassThroughFactory)
}

func main() {
}
