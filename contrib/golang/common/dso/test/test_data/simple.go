package main

/*
typedef struct {
  int foo;
} httpRequest;

typedef struct {
  unsigned long long int plugin_name_ptr;
  unsigned long long int plugin_name_len;
  unsigned long long int config_ptr;
  unsigned long long int config_len;
  int is_route_config;
} httpConfig;
*/
import "C"
import (
	"sync"
	"unsafe"
)

var configCache = &sync.Map{}

//export envoyGoFilterNewHttpPluginConfig
func envoyGoFilterNewHttpPluginConfig(c *C.httpConfig) uint64 {
	// already existing return 0, just for testing the destroy api.
	if _, ok := configCache.Load(uint64(c.config_len)); ok {
		return 0
	}
	// mark this configLen already existing
	configCache.Store(uint64(c.config_len), uint64(c.config_len))
	return uint64(c.config_len)
}

//export envoyGoFilterDestroyHttpPluginConfig
func envoyGoFilterDestroyHttpPluginConfig(id uint64, needDelay int) {
	configCache.Delete(id)
}

//export envoyGoFilterMergeHttpPluginConfig
func envoyGoFilterMergeHttpPluginConfig(namePtr, nameLen, parentId, childId uint64) uint64 {
	return 0
}

//export envoyGoFilterOnHttpHeader
func envoyGoFilterOnHttpHeader(r *C.httpRequest, endStream, headerNum, headerBytes uint64) uint64 {
	return 0
}

//export envoyGoFilterOnHttpData
func envoyGoFilterOnHttpData(r *C.httpRequest, endStream, buffer, length uint64) uint64 {
	return 0
}

//export envoyGoFilterOnHttpLog
func envoyGoFilterOnHttpLog(r *C.httpRequest, logType uint64) {
}

//export envoyGoFilterOnHttpDestroy
func envoyGoFilterOnHttpDestroy(r *C.httpRequest, reason uint64) {
}

//export envoyGoClusterSpecifierNewPlugin
func envoyGoClusterSpecifierNewPlugin(configPtr uint64, configLen uint64) uint64 {
	return 200
}

//export envoyGoOnClusterSpecify
func envoyGoOnClusterSpecify(pluginPtr uint64, headerPtr uint64, pluginId uint64, bufferPtr uint64, bufferLen uint64) int64 {
	return 0
}

//export envoyGoFilterOnNetworkFilterConfig
func envoyGoFilterOnNetworkFilterConfig(libraryIDPtr uint64, libraryIDLen uint64, configPtr uint64, configLen uint64) uint64 {
	return 100
}

//export envoyGoFilterOnDownstreamConnection
func envoyGoFilterOnDownstreamConnection(wrapper unsafe.Pointer, pluginNamePtr uint64, pluginNameLen uint64,
	configID uint64) uint64 {
	return 0
}

//export envoyGoFilterOnDownstreamData
func envoyGoFilterOnDownstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	return 0
}

//export envoyGoFilterOnDownstreamEvent
func envoyGoFilterOnDownstreamEvent(wrapper unsafe.Pointer, event int) {}

//export envoyGoFilterOnDownstreamWrite
func envoyGoFilterOnDownstreamWrite(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) uint64 {
	return 0
}

//export envoyGoFilterOnUpstreamConnectionReady
func envoyGoFilterOnUpstreamConnectionReady(wrapper unsafe.Pointer, connID uint64) {}

//export envoyGoFilterOnUpstreamConnectionFailure
func envoyGoFilterOnUpstreamConnectionFailure(wrapper unsafe.Pointer, reason int, connID uint64) {}

//export envoyGoFilterOnUpstreamData
func envoyGoFilterOnUpstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) {
}

//export envoyGoFilterOnUpstreamEvent
func envoyGoFilterOnUpstreamEvent(wrapper unsafe.Pointer, event int) {}

//export envoyGoFilterOnSemaDec
func envoyGoFilterOnSemaDec(wrapper unsafe.Pointer) {
}

//export envoyGoRequestSemaDec
func envoyGoRequestSemaDec(r *C.httpRequest) {
}

func main() {
}
