package main

/*
typedef struct {
  int foo;
} httpRequest;
*/
import "C"
import "unsafe"

//export envoyGoFilterNewHttpPluginConfig
func envoyGoFilterNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64 {
	return 100
}

//export envoyGoFilterDestroyHttpPluginConfig
func envoyGoFilterDestroyHttpPluginConfig(id uint64) {
}

//export envoyGoFilterMergeHttpPluginConfig
func envoyGoFilterMergeHttpPluginConfig(parentId uint64, childId uint64) uint64 {
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
func envoyGoFilterOnUpstreamConnectionReady(wrapper unsafe.Pointer) {}

//export envoyGoFilterOnUpstreamConnectionFailure
func envoyGoFilterOnUpstreamConnectionFailure(wrapper unsafe.Pointer, reason int) {}

//export envoyGoFilterOnUpstreamData
func envoyGoFilterOnUpstreamData(wrapper unsafe.Pointer, dataSize uint64, dataPtr uint64, sliceNum int, endOfStream int) {
}

//export envoyGoFilterOnUpstreamEvent
func envoyGoFilterOnUpstreamEvent(wrapper unsafe.Pointer, event int) {}

func main() {
}
