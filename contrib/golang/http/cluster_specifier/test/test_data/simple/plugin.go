package main

import (
	"strings"

	"github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/api"
)

type pluginConfig struct {
	invalidPrefix string
}

type clusterSpecifier struct {
	config *pluginConfig
}

func (s *clusterSpecifier) Choose(header api.RequestHeaderMap) string {
	// block the request with an unknown cluster.
	path := header.Get(":path")
	if strings.HasPrefix(path, s.config.invalidPrefix) {
		return "cluster_unknown"
	}
	return "cluster_0"
}
