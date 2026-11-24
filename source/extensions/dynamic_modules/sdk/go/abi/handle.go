package abi

/*
#include <stdlib.h>
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

// Envoy retains a C-allocated box containing the Go handle until the destroy hook.
func newHandle(value any) unsafe.Pointer {
	box := C.malloc(C.size_t(unsafe.Sizeof(cgo.Handle(0))))
	*(*cgo.Handle)(box) = cgo.NewHandle(value)
	return box
}

func handleValue[T any](box unsafe.Pointer) T {
	return (*cgo.Handle)(box).Value().(T)
}

func deleteHandle(box unsafe.Pointer) {
	(*cgo.Handle)(box).Delete()
	C.free(box)
}
