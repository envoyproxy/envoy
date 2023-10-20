package main

import (
	"fmt"
	"sync"
	"time"

	xds "github.com/cncf/xds/go/xds/type/v3"
	"google.golang.org/protobuf/types/known/anypb"

	"github.com/envoyproxy/envoy/contrib/golang/common/go/api"
	"github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http"
)

type config struct {
	AddReqHeaderName   string
	AddReqHeaderValue  string
	AddRespHeaderName  string
	AddRespHeaderValue string

	LocalReplyPhase string

	SetReqBody  string
	SetRespBody string
}

type parser struct {
}

func (p *parser) Parse(any *anypb.Any, callbacks api.ConfigCallbackHandler) (interface{}, error) {
	conf := &config{}
	if any == nil {
		return conf, nil
	}

	configStruct := &xds.TypedStruct{}
	if err := any.UnmarshalTo(configStruct); err != nil {
		return nil, err
	}

	v := configStruct.Value
	if str, ok := v.AsMap()["AddReqHeaderName"].(string); ok {
		conf.AddReqHeaderName = str
	}
	if str, ok := v.AsMap()["AddReqHeaderValue"].(string); ok {
		conf.AddReqHeaderValue = str
	}
	if str, ok := v.AsMap()["AddRespHeaderName"].(string); ok {
		conf.AddRespHeaderName = str
	}
	if str, ok := v.AsMap()["AddRespHeaderValue"].(string); ok {
		conf.AddRespHeaderValue = str
	}

	if str, ok := v.AsMap()["LocalReplyPhase"].(string); ok {
		conf.LocalReplyPhase = str
	}

	if str, ok := v.AsMap()["SetReqBody"].(string); ok {
		conf.SetReqBody = str
	}
	if str, ok := v.AsMap()["SetRespBody"].(string); ok {
		conf.SetRespBody = str
	}

	return conf, nil
}

func (p *parser) Merge(parent interface{}, child interface{}) interface{} {
	return child
}

func addHeaderConfigFactory(c interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &addHeaderFilter{
			callbacks: callbacks,
			config:    c.(*config),
		}
	}
}

type addHeaderFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *addHeaderFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	if f.config.AddReqHeaderName != "" {
		header.Add(f.config.AddReqHeaderName, f.config.AddReqHeaderValue)
	}
	return api.Continue
}

func (f *addHeaderFilter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	if f.config.AddRespHeaderName != "" {
		header.Add(f.config.AddRespHeaderName, f.config.AddRespHeaderValue)
	}
	return api.Continue
}

func setBodyConfigFactory(c interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &setBodyFilter{
			callbacks: callbacks,
			config:    c.(*config),
		}
	}
}

type setBodyFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *setBodyFilter) DecodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	api.LogCriticalf("original body: %s", buf.String())
	if f.config.SetReqBody != "" {
		buf.SetString(f.config.SetReqBody)
	}
	return api.Continue
}

func (f *setBodyFilter) EncodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	api.LogCriticalf("original body: %s", buf.String())
	if f.config.SetRespBody != "" {
		buf.SetString(f.config.SetRespBody)
	}
	return api.Continue
}

func localReplyConfigFactory(c interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &localReplyFilter{
			callbacks: callbacks,
			config:    c.(*config),
		}
	}
}

type localReplyFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
	config    *config
}

func (f *localReplyFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	if f.config.LocalReplyPhase == "DecodeHeaders" {
		f.callbacks.SendLocalReply(405, "localReply", nil, 0, "")
		return api.LocalReply
	}
	return api.Continue
}

func (f *localReplyFilter) DecodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	if endStream && f.config.LocalReplyPhase == "DecodeData" {
		f.callbacks.SendLocalReply(404, "localReply", nil, 0, "")
		return api.LocalReply
	}
	return api.Continue
}

func (f *localReplyFilter) DecodeTrailers(api.RequestTrailerMap) api.StatusType {
	if f.config.LocalReplyPhase == "DecodeTrailers" {
		f.callbacks.SendLocalReply(403, "localReply", nil, 0, "")
		return api.LocalReply
	}
	return api.Continue
}

func (f *localReplyFilter) EncodeHeaders(api.ResponseHeaderMap, bool) api.StatusType {
	if f.config.LocalReplyPhase == "EncodeHeaders" {
		f.callbacks.SendLocalReply(402, "localReply", nil, 0, "")
		return api.LocalReply
	}
	return api.Continue
}

func nonblockingConfigFactory(interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &nonblockingFilter{
			callbacks: callbacks,
		}
	}
}

type nonblockingFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
}

func (f *nonblockingFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	// simulate time-consuming operation
	wg := sync.WaitGroup{}
	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		wg.Done()
	}()
	wg.Wait()
	return api.Running
}

func (f *nonblockingFilter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	wg := sync.WaitGroup{}
	wg.Add(1)
	go func() {
		time.Sleep(1 * time.Millisecond)
		wg.Done()
	}()
	wg.Wait()
	return api.Running
}

func panicConfigFactory(interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &panicFilter{
			callbacks: callbacks,
		}
	}
}

type panicFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
}

func (f *panicFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	panic("ouch")
}

func stopConfigFactory(interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &stopFilter{
			callbacks: callbacks,
		}
	}
}

type stopFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
}

func (f *stopFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	return api.StopAndBufferWatermark
}

func (f *stopFilter) DecodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	if endStream {
		return api.Continue
	}
	return api.StopAndBuffer
}

func (f *stopFilter) EncodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	if endStream {
		return api.Continue
	}
	api.LogCriticalf("StopNoBuffer, original body: %s", buf.String())
	return api.StopNoBuffer
}

func logConfigFactory(interface{}) api.StreamFilterFactory {
	return func(callbacks api.FilterCallbackHandler) api.StreamFilter {
		return &logFilter{
			callbacks: callbacks,
		}
	}
}

type logFilter struct {
	api.PassThroughStreamFilter
	callbacks api.FilterCallbackHandler
}

func (f *logFilter) DecodeHeaders(header api.RequestHeaderMap, endStream bool) api.StatusType {
	f.callbacks.Log(api.Critical, fmt.Sprintf("DecodeHeaders %v", endStream))
	return api.Continue
}

func (f *logFilter) DecodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	f.callbacks.Log(api.Critical, fmt.Sprintf("DecodeData %v", endStream))
	return api.Continue
}

func (f *logFilter) DecodeTrailers(api.RequestTrailerMap) api.StatusType {
	f.callbacks.Log(api.Critical, "DecodeTrailers")
	return api.Continue
}

func (f *logFilter) EncodeHeaders(header api.ResponseHeaderMap, endStream bool) api.StatusType {
	f.callbacks.Log(api.Critical, fmt.Sprintf("EncodeHeaders %v", endStream))
	return api.Continue
}

func (f *logFilter) EncodeData(buf api.BufferInstance, endStream bool) api.StatusType {
	f.callbacks.Log(api.Critical, fmt.Sprintf("EncodeData %v", endStream))
	return api.Continue
}

func (f *logFilter) EncodeTrailers(api.ResponseTrailerMap) api.StatusType {
	f.callbacks.Log(api.Critical, "EncodeTrailers")
	return api.Continue
}

func (f *logFilter) OnLog() {
	f.callbacks.Log(api.Critical, "OnLog")
}

func (f *logFilter) OnLogDownstreamStart() {
	f.callbacks.Log(api.Critical, "OnLogDownstreamStart")
}

func (f *logFilter) OnLogDownstreamPeriodic() {
	f.callbacks.Log(api.Critical, "OnLogDownstreamPeriodic")
}

func (f *logFilter) OnDestroy(reason api.DestroyReason) {
	f.callbacks.Log(api.Critical, fmt.Sprintf("OnDestroy %v", reason))
}

func RegisterManagedFilters() {
	http.RegisterHttpFilterConfigFactoryAndParser("addHeader", addHeaderConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("setBody", setBodyConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("localReply", localReplyConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("nonblocking", nonblockingConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("panic", panicConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("stop", stopConfigFactory, &parser{})
	http.RegisterHttpFilterConfigFactoryAndParser("log", logConfigFactory, &parser{})
}
