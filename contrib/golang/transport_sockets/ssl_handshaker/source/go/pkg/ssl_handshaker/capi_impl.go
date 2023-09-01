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
	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
)

type sslCApiImpl struct{}

func (c *sslCApiImpl) DownstreamTlsConnSelectCert(envoyTlsHandshaker uint64, respType int, certName uint64, certNameLen int) {
	C.moeTlsConnectionSelectCert(C.ulonglong(envoyTlsHandshaker), C.int(respType), C.ulonglong(certName), C.int(certNameLen))
}

var cAPI api.SSLCAPI = &sslCApiImpl{}
