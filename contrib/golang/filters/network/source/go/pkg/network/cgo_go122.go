//go:build go1.22

package network

/*
// This is a performance optimization.
// The following noescape and nocallback directives are used to
// prevent the Go compiler from allocating function parameters on the heap.

#cgo noescape envoyGoFilterDownstreamWrite
#cgo nocallback envoyGoFilterDownstreamWrite
#cgo noescape envoyGoFilterDownstreamInfo
#cgo nocallback envoyGoFilterDownstreamInfo
#cgo noescape envoyGoFilterUpstreamConnect
#cgo nocallback envoyGoFilterUpstreamConnect
#cgo noescape envoyGoFilterUpstreamWrite
#cgo nocallback envoyGoFilterUpstreamWrite
#cgo noescape envoyGoFilterUpstreamInfo
#cgo nocallback envoyGoFilterUpstreamInfo
#cgo noescape envoyGoFilterSetFilterState
#cgo nocallback envoyGoFilterSetFilterState
#cgo noescape envoyGoFilterGetFilterState
#cgo nocallback envoyGoFilterGetFilterState

*/
import "C"
