package main

import (
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

func init() {
	RegisterManagedFilters()
	http.RegisterHttpFilterManager("filtermanager")
}

func main() {
}
