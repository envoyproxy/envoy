package main

import (
	"fmt"
	"strings"

	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterHttpFilterConfigFactories(map[string]shared.HttpFilterConfigFactory{
		"hello_anurag": &helloAnuragConfigFactory{},
	})
}

func main() {}

type helloAnuragConfigFactory struct {
	shared.EmptyHttpFilterConfigFactory
}
type helloAnuragFactory struct {
	shared.EmptyHttpFilterFactory
}

func (f *helloAnuragConfigFactory) Create(handle shared.HttpFilterConfigHandle,
	config []byte) (shared.HttpFilterFactory, error) {
	return &helloAnuragFactory{}, nil
}

type helloAnuragFilter struct {
	shared.EmptyHttpFilter
	handle shared.HttpFilterHandle
}

func (f *helloAnuragFactory) Create(handle shared.HttpFilterHandle) shared.HttpFilter {
	return &helloAnuragFilter{handle: handle}
}

func (p *helloAnuragFilter) OnResponseHeaders(headers shared.HeaderMap,
	endOfStream bool) shared.HeadersStatus {
	return shared.HeadersStatusStop
}

func (p *helloAnuragFilter) OnResponseBody(body shared.BodyBuffer,
	endOfStream bool) shared.BodyStatus {
	if !endOfStream {
		return shared.BodyStatusStopAndBuffer
	}

	var out strings.Builder
	out.WriteString("=== Testing New ABI Methods ===\n\n")

	// --- 1. SetMetadata + GetMetadataBool ---
	out.WriteString("--- SetMetadata / GetMetadataBool ---\n")

	// Set a bool value via SetMetadata
	p.handle.SetMetadata("test_ns", "flag_true", true)
	p.handle.SetMetadata("test_ns", "flag_false", false)
	p.handle.SetMetadata("test_ns", "some_string", "hello")
	p.handle.SetMetadata("another_ns", "key1", true)

	// Read back with GetMetadataBool
	val, found := p.handle.GetMetadataBool(shared.MetadataSourceTypeDynamic, "test_ns", "flag_true")
	out.WriteString(fmt.Sprintf("GetMetadataBool(dynamic, test_ns, flag_true) = %v, found=%v\n", val, found))

	val, found = p.handle.GetMetadataBool(shared.MetadataSourceTypeDynamic, "test_ns", "flag_false")
	out.WriteString(fmt.Sprintf("GetMetadataBool(dynamic, test_ns, flag_false) = %v, found=%v\n", val, found))

	// Non-bool key should return found=false
	val, found = p.handle.GetMetadataBool(shared.MetadataSourceTypeDynamic, "test_ns", "some_string")
	out.WriteString(fmt.Sprintf("GetMetadataBool(dynamic, test_ns, some_string) = %v, found=%v  (expect found=false, not a bool)\n", val, found))

	// Missing key
	val, found = p.handle.GetMetadataBool(shared.MetadataSourceTypeDynamic, "test_ns", "nonexistent")
	out.WriteString(fmt.Sprintf("GetMetadataBool(dynamic, test_ns, nonexistent) = %v, found=%v  (expect found=false)\n", val, found))

	out.WriteString("\n")

	// --- 2. GetMetadataKeys ---
	out.WriteString("--- GetMetadataKeys ---\n")

	keys := p.handle.GetMetadataKeys(shared.MetadataSourceTypeDynamic, "test_ns")
	out.WriteString(fmt.Sprintf("GetMetadataKeys(dynamic, test_ns) = %v\n", keys))

	keys = p.handle.GetMetadataKeys(shared.MetadataSourceTypeDynamic, "another_ns")
	out.WriteString(fmt.Sprintf("GetMetadataKeys(dynamic, another_ns) = %v\n", keys))

	keys = p.handle.GetMetadataKeys(shared.MetadataSourceTypeDynamic, "nonexistent_ns")
	out.WriteString(fmt.Sprintf("GetMetadataKeys(dynamic, nonexistent_ns) = %v  (expect nil/empty)\n", keys))

	out.WriteString("\n")

	// --- 3. GetMetadataNamespaces ---
	out.WriteString("--- GetMetadataNamespaces ---\n")

	namespaces := p.handle.GetMetadataNamespaces(shared.MetadataSourceTypeDynamic)
	out.WriteString(fmt.Sprintf("GetMetadataNamespaces(dynamic) = %v\n", namespaces))

	out.WriteString("\n")

	// --- 4. GetAttributeBool (ConnectionMtls) ---
	out.WriteString("--- GetAttributeBool ---\n")

	mtlsVal, mtlsFound := p.handle.GetAttributeBool(shared.AttributeIDConnectionMtls)
	out.WriteString(fmt.Sprintf("GetAttributeBool(ConnectionMtls) = %v, found=%v  (expect false for plain HTTP)\n", mtlsVal, mtlsFound))

	out.WriteString("\n=== Done ===\n")

	// Drain all buffered response body.
	buffered := p.handle.BufferedResponseBody()
	buffered.Drain(buffered.GetSize())

	// Drain all received body.
	body.Drain(body.GetSize())

	// Replace with test output.
	result := out.String()
	body.Append([]byte(result))
	p.handle.ResponseHeaders().Set("content-length", fmt.Sprintf("%d", len(result)))
	p.handle.ResponseHeaders().Set("content-type", "text/plain")

	return shared.BodyStatusContinue
}
