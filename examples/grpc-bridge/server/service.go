package main

import (
	"flag"
	"fmt"
	"log"
	"net"

	"sync"

  "github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
)

type KV struct {
	sync.Mutex
	store map[string]string
}

func (k *KV) Get(ctx context.Context, in *kv.GetRequest) (*kv.GetResponse, error) {
	log.Printf("get: %s", in.Key)
	resp := new(kv.GetResponse)
	if val, ok := k.store[in.Key]; ok {
		resp.Value = val
	}

	return resp, nil
}

func (k *KV) Set(ctx context.Context, in *kv.SetRequest) (*kv.SetResponse, error) {
	log.Printf("set: %s = %s", in.Key, in.Value)
	k.Lock()
	defer k.Unlock()

	k.store[in.Key] = in.Value

	return &kv.SetResponse{Ok: true}, nil
}

func NewKVStore() (kv *KV) {
	kv = &KV{
		store: make(map[string]string),
	}

	return
}

func main() {
	port := flag.Int("port", 8081, "grpc port")

	flag.Parse()

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *port))
	if err != nil {
		log.Fatalf("failed to listen: %v", err)
	}
	gs := grpc.NewServer()
	kv.RegisterKVServer(gs, NewKVStore())

	log.Printf("starting grpc on :%d\n", *port)

	gs.Serve(lis)
}
