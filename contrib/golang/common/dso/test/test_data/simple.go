package main

/*
typedef struct {
  int foo;
} httpRequest;
*/
import "C"

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

func main() {
}
