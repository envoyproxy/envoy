package api

import (
	httpFilterApi "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
	clusterSpecifierApi "github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/cluster_specifier"
)

var RegisterHttpFilterConfigFactory = httpFilterApi.RegisterHttpFilterConfigFactory
var RegisterHttpFilterConfigParser = httpFilterApi.RegisterHttpFilterConfigParser
var RegisterClusterSpecifierFactory = clusterSpecifierApi.RegisterClusterSpecifierFactory
