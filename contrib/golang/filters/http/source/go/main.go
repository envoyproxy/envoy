package main

import (
    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
    "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/plugins/echo"
)

func init() {
    http.RegisterHttpFilterConfigFactory(echo.Name, echo.ConfigFactory)
}

func main() {
}
