/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package http

// The Go code generated from @envoy_api//contrib/envoy/extensions/filters/http/golang/v3alpha:pkg_go_proto
// can't be used to build this library, because it use `go-control-plane` as the module name.
// It seems the BUILD file in golang/v3alpha is auto-generated so we can't add new rule in it.
// Without changing current system, we decide to copy the generated code here.

import (
	reflect "reflect"
	sync "sync"

	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type FilterManagerConfigPerFilter struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name   string     `protobuf:"bytes,1,opt,name=name,proto3" json:"name,omitempty"`
	Config *anypb.Any `protobuf:"bytes,2,opt,name=config,proto3" json:"config,omitempty"`
}

func (x *FilterManagerConfigPerFilter) Reset() {
	*x = FilterManagerConfigPerFilter{}
	if protoimpl.UnsafeEnabled {
		mi := &file_x_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *FilterManagerConfigPerFilter) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*FilterManagerConfigPerFilter) ProtoMessage() {}

func (x *FilterManagerConfigPerFilter) ProtoReflect() protoreflect.Message {
	mi := &file_x_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use FilterManagerConfigPerFilter.ProtoReflect.Descriptor instead.
func (*FilterManagerConfigPerFilter) Descriptor() ([]byte, []int) {
	return file_x_proto_rawDescGZIP(), []int{0}
}

func (x *FilterManagerConfigPerFilter) GetName() string {
	if x != nil {
		return x.Name
	}
	return ""
}

func (x *FilterManagerConfigPerFilter) GetConfig() *anypb.Any {
	if x != nil {
		return x.Config
	}
	return nil
}

type FilterManagerConfig struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Configs []*FilterManagerConfigPerFilter `protobuf:"bytes,1,rep,name=configs,proto3" json:"configs,omitempty"`
}

func (x *FilterManagerConfig) Reset() {
	*x = FilterManagerConfig{}
	if protoimpl.UnsafeEnabled {
		mi := &file_x_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *FilterManagerConfig) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*FilterManagerConfig) ProtoMessage() {}

func (x *FilterManagerConfig) ProtoReflect() protoreflect.Message {
	mi := &file_x_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use FilterManagerConfig.ProtoReflect.Descriptor instead.
func (*FilterManagerConfig) Descriptor() ([]byte, []int) {
	return file_x_proto_rawDescGZIP(), []int{1}
}

func (x *FilterManagerConfig) GetConfigs() []*FilterManagerConfigPerFilter {
	if x != nil {
		return x.Configs
	}
	return nil
}

var File_x_proto protoreflect.FileDescriptor

var file_x_proto_rawDesc = []byte{
	0x0a, 0x07, 0x78, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x2c, 0x65, 0x6e, 0x76, 0x6f, 0x79,
	0x2e, 0x65, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x73, 0x2e, 0x66, 0x69, 0x6c, 0x74,
	0x65, 0x72, 0x73, 0x2e, 0x68, 0x74, 0x74, 0x70, 0x2e, 0x67, 0x6f, 0x6c, 0x61, 0x6e, 0x67, 0x2e,
	0x76, 0x33, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x1a, 0x19, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2f,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x2f, 0x61, 0x6e, 0x79, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x22, 0x60, 0x0a, 0x1c, 0x46, 0x69, 0x6c, 0x74, 0x65, 0x72, 0x4d, 0x61, 0x6e, 0x61,
	0x67, 0x65, 0x72, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x50, 0x65, 0x72, 0x46, 0x69, 0x6c, 0x74,
	0x65, 0x72, 0x12, 0x12, 0x0a, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09,
	0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x12, 0x2c, 0x0a, 0x06, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67,
	0x18, 0x02, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x14, 0x2e, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2e,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x2e, 0x41, 0x6e, 0x79, 0x52, 0x06, 0x63, 0x6f,
	0x6e, 0x66, 0x69, 0x67, 0x22, 0x7b, 0x0a, 0x13, 0x46, 0x69, 0x6c, 0x74, 0x65, 0x72, 0x4d, 0x61,
	0x6e, 0x61, 0x67, 0x65, 0x72, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x12, 0x64, 0x0a, 0x07, 0x63,
	0x6f, 0x6e, 0x66, 0x69, 0x67, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x4a, 0x2e, 0x65,
	0x6e, 0x76, 0x6f, 0x79, 0x2e, 0x65, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x73, 0x2e,
	0x66, 0x69, 0x6c, 0x74, 0x65, 0x72, 0x73, 0x2e, 0x68, 0x74, 0x74, 0x70, 0x2e, 0x67, 0x6f, 0x6c,
	0x61, 0x6e, 0x67, 0x2e, 0x76, 0x33, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x2e, 0x46, 0x69, 0x6c, 0x74,
	0x65, 0x72, 0x4d, 0x61, 0x6e, 0x61, 0x67, 0x65, 0x72, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x50,
	0x65, 0x72, 0x46, 0x69, 0x6c, 0x74, 0x65, 0x72, 0x52, 0x07, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67,
	0x73, 0x42, 0x55, 0x5a, 0x53, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
	0x65, 0x6e, 0x76, 0x6f, 0x79, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x2f, 0x67, 0x6f, 0x2d, 0x63, 0x6f,
	0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2d, 0x70, 0x6c, 0x61, 0x6e, 0x65, 0x2f, 0x65, 0x6e, 0x76, 0x6f,
	0x79, 0x2f, 0x65, 0x78, 0x74, 0x65, 0x6e, 0x73, 0x69, 0x6f, 0x6e, 0x73, 0x2f, 0x66, 0x69, 0x6c,
	0x74, 0x65, 0x72, 0x73, 0x2f, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x67, 0x6f, 0x6c, 0x61, 0x6e, 0x67,
	0x2f, 0x76, 0x33, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_x_proto_rawDescOnce sync.Once
	file_x_proto_rawDescData = file_x_proto_rawDesc
)

func file_x_proto_rawDescGZIP() []byte {
	file_x_proto_rawDescOnce.Do(func() {
		file_x_proto_rawDescData = protoimpl.X.CompressGZIP(file_x_proto_rawDescData)
	})
	return file_x_proto_rawDescData
}

var file_x_proto_msgTypes = make([]protoimpl.MessageInfo, 2)
var file_x_proto_goTypes = []interface{}{
	(*FilterManagerConfigPerFilter)(nil), // 0: envoy.extensions.filters.http.golang.v3alpha.FilterManagerConfigPerFilter
	(*FilterManagerConfig)(nil),          // 1: envoy.extensions.filters.http.golang.v3alpha.FilterManagerConfig
	(*anypb.Any)(nil),                    // 2: google.protobuf.Any
}
var file_x_proto_depIdxs = []int32{
	2, // 0: envoy.extensions.filters.http.golang.v3alpha.FilterManagerConfigPerFilter.config:type_name -> google.protobuf.Any
	0, // 1: envoy.extensions.filters.http.golang.v3alpha.FilterManagerConfig.configs:type_name -> envoy.extensions.filters.http.golang.v3alpha.FilterManagerConfigPerFilter
	2, // [2:2] is the sub-list for method output_type
	2, // [2:2] is the sub-list for method input_type
	2, // [2:2] is the sub-list for extension type_name
	2, // [2:2] is the sub-list for extension extendee
	0, // [0:2] is the sub-list for field type_name
}

func init() { file_x_proto_init() }
func file_x_proto_init() {
	if File_x_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_x_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*FilterManagerConfigPerFilter); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_x_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*FilterManagerConfig); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_x_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   2,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_x_proto_goTypes,
		DependencyIndexes: file_x_proto_depIdxs,
		MessageInfos:      file_x_proto_msgTypes,
	}.Build()
	File_x_proto = out.File
	file_x_proto_rawDesc = nil
	file_x_proto_goTypes = nil
	file_x_proto_depIdxs = nil
}
