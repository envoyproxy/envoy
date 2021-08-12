package main

import (
	"crypto/tls"
	"crypto/x509"
	"flag"
	"log"
	"net/http"

	"github.com/lucas-clemente/quic-go"
	"github.com/lucas-clemente/quic-go/http3"
)

func main() {
	address := flag.String("address", "", "address in the form of host:port/path")
	sni := flag.String("sni", "", "server name TLS parameter")
	flag.Parse()

	pool, err := x509.SystemCertPool()
	if err != nil {
		log.Fatal(err)
	}

	var quicConfig quic.Config
	roundTripper := &http3.RoundTripper{
		TLSClientConfig: &tls.Config{
			RootCAs:            pool,
			InsecureSkipVerify: true,
			ServerName:         *sni,
		},
		QuicConfig: &quicConfig,
	}
	defer roundTripper.Close()

	h3Client := &http.Client{Transport: roundTripper}
	resp, err := h3Client.Get(*address)
	if err != nil {
		log.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK {
		log.Fatalf("FAILED: status code returned on %s: %d", *address, resp.StatusCode)
	}
	log.Printf("SUCCESS: received status 200/OK from %s", *address)
}
