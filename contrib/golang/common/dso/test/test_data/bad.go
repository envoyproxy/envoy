package main

import "C"

// missing the envoyGoClusterSpecifierNewPlugin symbol.

//export envoyGoOnClusterSpecify
func envoyGoOnClusterSpecify(pluginPtr uint64, headerPtr uint64, pluginId uint64, bufferPtr uint64, bufferLen uint64) int64 {
	return 0
}

func main() {
}
