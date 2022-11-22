package main

/*
typedef struct {
  int foo;
} httpRequest;
*/
import "C"

//export moeNewHttpPluginConfig
func moeNewHttpPluginConfig(configPtr uint64, configLen uint64) uint64 {
    return 100
}

//export moeDestroyHttpPluginConfig
func moeDestroyHttpPluginConfig(id uint64) {
}

//export moeMergeHttpPluginConfig
func moeMergeHttpPluginConfig(parentId uint64, childId uint64) uint64 {
    return 0
}

//export moeOnHttpHeader
func moeOnHttpHeader(r *C.httpRequest, endStream, headerNum, headerBytes uint64) uint64 {
    return 0
}

//export moeOnHttpData
func moeOnHttpData(r *C.httpRequest, endStream, buffer, length uint64) uint64 {
    return 0
}

//export moeOnHttpDestroy
func moeOnHttpDestroy(r *C.httpRequest, reason uint64) {
}

func main() {
}
