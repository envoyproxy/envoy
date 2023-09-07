//go:build go1.22

package http

/*
// This is a performance optimization.
// The following noescape and nocallback directives are used to
// prevent the Go compiler from allocating function parameters on the heap.

#cgo noescape envoyGoFilterHttpCopyHeaders
#cgo nocallback envoyGoFilterHttpCopyHeaders
#cgo noescape envoyGoFilterHttpSendPanicReply
#cgo nocallback envoyGoFilterHttpSendPanicReply
#cgo noescape envoyGoFilterHttpGetHeader
#cgo nocallback envoyGoFilterHttpGetHeader
#cgo noescape envoyGoFilterHttpSetHeaderHelper
#cgo nocallback envoyGoFilterHttpSetHeaderHelper
#cgo noescape envoyGoFilterHttpRemoveHeader
#cgo nocallback envoyGoFilterHttpRemoveHeader
#cgo noescape envoyGoFilterHttpGetBuffer
#cgo nocallback envoyGoFilterHttpGetBuffer
#cgo noescape envoyGoFilterHttpSetBufferHelper
#cgo nocallback envoyGoFilterHttpSetBufferHelper
#cgo noescape envoyGoFilterHttpCopyTrailers
#cgo nocallback envoyGoFilterHttpCopyTrailers
#cgo noescape envoyGoFilterHttpSetTrailer
#cgo nocallback envoyGoFilterHttpSetTrailer
#cgo noescape envoyGoFilterHttpRemoveTrailer
#cgo nocallback envoyGoFilterHttpRemoveTrailer
#cgo noescape envoyGoFilterHttpGetStringValue
#cgo nocallback envoyGoFilterHttpGetStringValue
#cgo noescape envoyGoFilterHttpGetIntegerValue
#cgo nocallback envoyGoFilterHttpGetIntegerValue
#cgo noescape envoyGoFilterHttpGetDynamicMetadata
#cgo nocallback envoyGoFilterHttpGetDynamicMetadata
#cgo noescape envoyGoFilterHttpSetDynamicMetadata
#cgo nocallback envoyGoFilterHttpSetDynamicMetadata
#cgo noescape envoyGoFilterLog
#cgo nocallback envoyGoFilterLog
#cgo noescape envoyGoFilterHttpSetStringFilterState
#cgo nocallback envoyGoFilterHttpSetStringFilterState
#cgo noescape envoyGoFilterHttpGetStringFilterState
#cgo nocallback envoyGoFilterHttpGetStringFilterState
#cgo noescape envoyGoFilterHttpGetStringProperty
#cgo nocallback envoyGoFilterHttpGetStringProperty
#cgo noescape envoyGoFilterHttpDefineMetric
#cgo nocallback envoyGoFilterHttpDefineMetric
#cgo noescape envoyGoFilterHttpIncrementMetric
#cgo nocallback envoyGoFilterHttpIncrementMetric
#cgo noescape envoyGoFilterHttpGetMetric
#cgo nocallback envoyGoFilterHttpGetMetric

*/
import "C"
