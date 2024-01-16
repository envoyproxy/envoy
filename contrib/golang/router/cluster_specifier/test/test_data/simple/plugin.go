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

	return "cluster_0"
}
