package main

import (
	"github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/http"
	"github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/plugins/simple"
)

func init() {
	http.RegisterHttpFilterConfigFactory(simple.ConfigFactory)
}

func main() {
}
