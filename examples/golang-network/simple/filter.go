package main

import (
	"fmt"

	xds "github.com/cncf/xds/go/xds/type/v3"
	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/go/pkg/network"
	"google.golang.org/protobuf/types/known/anypb"
)

func init() {
	network.RegisterNetworkFilterConfigFactory("simple", cf)
}

var cf = &configFactory{}

type configFactory struct{}

func (f *configFactory) CreateFactoryFromConfig(config interface{}) network.FilterFactory {
	a := config.(*anypb.Any)
	configStruct := &xds.TypedStruct{}
	_ = a.UnmarshalTo(configStruct)

	v := configStruct.Value.AsMap()["echo_server_addr"]
	upAddr := v.(string)

	return &filterFactory{
		upAddr: upAddr,
	}
}

type filterFactory struct {
	upAddr string
}

func (f *filterFactory) CreateFilter(cb api.ConnectionCallback) api.DownstreamFilter {
	return &downFilter{
		upAddr: f.upAddr,
		cb:     cb,
	}
}

type downFilter struct {
	cb       api.ConnectionCallback
	upAddr   string
	upFilter *upFilter
}

func (f *downFilter) OnNewConnection() api.FilterStatus {
	fmt.Printf("OnNewConnection, local: %v, remote: %v\n, connect to: %v", f.cb.LocalAddr(), f.cb.RemoteAddr(), f.upAddr)
	f.upFilter = &upFilter{
		downFilter: f,
		ch:         make(chan []byte, 1),
	}
	network.CreateUpstreamConn(f.upAddr, f.upFilter)
	return api.NetworkFilterContinue
}

func (f *downFilter) OnData(buffer []byte, endOfStream bool) api.FilterStatus {
	fmt.Printf("OnData, addr: %v, buffer: %v, endOfStream: %v\n", f.cb.RemoteAddr(), string(buffer), endOfStream)
	buffer = append([]byte("hello, "), buffer...)
	f.upFilter.ch <- buffer
	return api.NetworkFilterContinue
}

func (f *downFilter) OnEvent(event api.ConnectionEvent) {
	fmt.Printf("OnEvent, addr: %v, event: %v\n", f.cb.RemoteAddr(), event)
}

func (f *downFilter) OnWrite(buffer []byte, endOfStream bool) api.FilterStatus {
	fmt.Printf("OnWrite, buffer: %v, endOfStream: %v\n", string(buffer), endOfStream)
	return api.NetworkFilterContinue
}

type upFilter struct {
	cb         api.ConnectionCallback
	downFilter *downFilter
	ch         chan []byte
}

func (f *upFilter) OnPoolReady(cb api.ConnectionCallback) {
	f.cb = cb
	fmt.Printf("OnPoolReady, local: %v, remote: %v\n", f.cb.LocalAddr(), f.cb.RemoteAddr())
	go func() {
		for {
			buf, ok := <-f.ch
			if !ok {
				return
			}
			f.cb.Write(buf, false)
		}
	}()
}

func (f *upFilter) OnPoolFailure(poolFailureReason api.PoolFailureReason, transportFailureReason string) {
	fmt.Printf("OnPoolFailure, reason: %v, transportFailureReason: %v\n", poolFailureReason, transportFailureReason)
}

func (f *upFilter) OnData(buffer []byte, endOfStream bool) {
	fmt.Printf("OnData, addr: %v, buffer: %v, endOfStream: %v\n", f.cb.RemoteAddr(), string(buffer), endOfStream)
	f.downFilter.cb.Write(buffer, endOfStream)
}

func (f *upFilter) OnEvent(event api.ConnectionEvent) {
	fmt.Printf("OnEvent, addr: %v, event: %v\n", f.cb.RemoteAddr(), event)
	if event == api.LocalClose || event == api.RemoteClose {
		close(f.ch)
	}
}

func main() {}
