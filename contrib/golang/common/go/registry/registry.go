package registry

// Unified registry for users.
// It will export all of the exported Go functions,
// so that the dso loader could be easier to implement in the C++ side.

import (
	httpfilter "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
	cluster "github.com/envoyproxy/envoy/contrib/golang/http/cluster_specifier/source/go/pkg/cluster_specifier"
)

/* HTTP filter */

var RegisterHttpFilterConfigFactory = httpfilter.RegisterHttpFilterConfigFactory
var RegisterHttpFilterConfigParser = httpfilter.RegisterHttpFilterConfigParser

/* cluster specifier plugin */

var RegisterClusterSpecifierFactory = cluster.RegisterClusterSpecifierFactory
var RegisterClusterSpecifierConfigParser = cluster.RegisterClusterSpecifierConfigParser
