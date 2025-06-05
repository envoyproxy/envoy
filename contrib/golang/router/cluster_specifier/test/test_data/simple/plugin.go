package main

import (
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/router/cluster_specifier/source/go/pkg/api"
)

type clusterSpecifier struct {
	invalidPrefix string
	defaultPrefix string
	panicPrefix   string
}

func headerHasTrueValue(headers map[string][]string, key string) bool {
	values, ok := headers[key]
	if !ok {
		return false
	}
	for _, element := range values {
		if element == "true" {
			return true
		}
	}
	return false
}

func (s *clusterSpecifier) Cluster(header api.RequestHeaderMap) string {
	path, _ := header.Get(":path")

	// block the request with an unknown cluster.
	if strings.HasPrefix(path, s.invalidPrefix) {
		return "cluster_unknown"
	}

	// return "" will using the default_cluster in the C++ side.
	if strings.HasPrefix(path, s.defaultPrefix) {
		return ""
	}

	// panic, will using the default_cluster in the C++ side.
	if strings.HasPrefix(path, s.panicPrefix) {
		panic("test")
	}

	if headerHasTrueValue(header.GetAllHeaders(), "custom_header_1") {
		return "cluster_unknown"
	}

	return "cluster_0"
}
