package main

import (
	"envoy-go-filter-samples/simple"

	"mosn.io/envoy-go-extension/pkg/http"
)

func init() {
	http.RegisterHttpFilterConfigFactory(simple.ConfigFactory)
}

func main() {
}
