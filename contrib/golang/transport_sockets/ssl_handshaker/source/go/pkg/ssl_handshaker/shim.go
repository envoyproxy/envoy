package ssl_handshaker

/*
// ref https://github.com/golang/go/issues/25832

#cgo CFLAGS: -I../../../../../../common/go/api -I../api
#cgo linux LDFLAGS: -Wl,-unresolved-symbols=ignore-all
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdlib.h>
#include <string.h>

#include "api.h"

*/
import "C"
import (
	"github.com/envoyproxy/envoy/contrib/golang/common/go/utils"
)

//export envoyOnTlsHandshakerSelectCert
func envoyOnTlsHandshakerSelectCert(envoyTlsHandshaker uint64, serverNamePtr uint64, serverNameLen uint64) {
	serverName := ""
	if serverNamePtr != 0 && serverNameLen != 0 {
		serverName = utils.BytesToString(serverNamePtr, serverNameLen)
	}
	conn := NewDownstreamTlsConn(envoyTlsHandshaker)
	conn.OnSelectCert(serverName)
}
