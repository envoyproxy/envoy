package main

import (
    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
    "github.com/envoyproxy/envoy/examples/golang/envoy-go-filter-samples/pkg/plugins/simple"
)

func init() {
    http.RegisterHttpFilterConfigFactory(simple.Name, simple.ConfigFactory)
}

func main() {
}
