package registry

import (
	httpfilter "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
	cluster "github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/cluster_specifier"
)

/* HTTP filter */

var RegisterHttpFilterConfigFactory = httpfilter.RegisterHttpFilterConfigFactory
var RegisterHttpFilterConfigParser = httpfilter.RegisterHttpFilterConfigParser

/* cluster specifier plugin */

var RegisterClusterSpecifierFactory = cluster.RegisterClusterSpecifierFactory
