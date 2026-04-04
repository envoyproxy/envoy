package main

import (
	"math"
	"time"

	cases "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go"
	other_package "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package/go"
	sort "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/sort/go"
	yet_another_package "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/yet_another_package/go"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"
	"google.golang.org/protobuf/types/known/durationpb"
	"google.golang.org/protobuf/types/known/timestamppb"
	"google.golang.org/protobuf/types/known/wrapperspb"
)

type TestCase struct {
	Name     string
	Message  proto.Message
	Failures int // expected number of failed validation errors
}

type TestResult struct {
	OK, Skipped bool
}

var TestCases []TestCase

func init() {
	sets := [][]TestCase{
		floatCases,
		doubleCases,
		int32Cases,
		int64Cases,
		uint32Cases,
		uint64Cases,
		sint32Cases,
		sint64Cases,
		fixed32Cases,
		fixed64Cases,
		sfixed32Cases,
		sfixed64Cases,
		boolCases,
		stringCases,
		bytesCases,
		enumCases,
		messageCases,
		repeatedCases,
		mapCases,
		oneofCases,
		wrapperCases,
		durationCases,
		timestampCases,
		anyCases,
		kitchenSink,
		nestedCases,
	}

	for _, set := range sets {
		TestCases = append(TestCases, set...)
	}
}

var floatCases = []TestCase{
	{"float - none - valid", &cases.FloatNone{Val: -1.23456}, 0},

	{"float - const - valid", &cases.FloatConst{Val: 1.23}, 0},
	{"float - const - invalid", &cases.FloatConst{Val: 4.56}, 1},

	{"float - in - valid", &cases.FloatIn{Val: 7.89}, 0},
	{"float - in - invalid", &cases.FloatIn{Val: 10.11}, 1},

	{"float - not in - valid", &cases.FloatNotIn{Val: 1}, 0},
	{"float - not in - invalid", &cases.FloatNotIn{Val: 0}, 1},

	{"float - lt - valid", &cases.FloatLT{Val: -1}, 0},
	{"float - lt - invalid (equal)", &cases.FloatLT{Val: 0}, 1},
	{"float - lt - invalid", &cases.FloatLT{Val: 1}, 1},

	{"float - lte - valid", &cases.FloatLTE{Val: 63}, 0},
	{"float - lte - valid (equal)", &cases.FloatLTE{Val: 64}, 0},
	{"float - lte - invalid", &cases.FloatLTE{Val: 65}, 1},

	{"float - gt - valid", &cases.FloatGT{Val: 17}, 0},
	{"float - gt - invalid (equal)", &cases.FloatGT{Val: 16}, 1},
	{"float - gt - invalid", &cases.FloatGT{Val: 15}, 1},

	{"float - gte - valid", &cases.FloatGTE{Val: 9}, 0},
	{"float - gte - valid (equal)", &cases.FloatGTE{Val: 8}, 0},
	{"float - gte - invalid", &cases.FloatGTE{Val: 7}, 1},

	{"float - gt & lt - valid", &cases.FloatGTLT{Val: 5}, 0},
	{"float - gt & lt - invalid (above)", &cases.FloatGTLT{Val: 11}, 1},
	{"float - gt & lt - invalid (below)", &cases.FloatGTLT{Val: -1}, 1},
	{"float - gt & lt - invalid (max)", &cases.FloatGTLT{Val: 10}, 1},
	{"float - gt & lt - invalid (min)", &cases.FloatGTLT{Val: 0}, 1},

	{"float - exclusive gt & lt - valid (above)", &cases.FloatExLTGT{Val: 11}, 0},
	{"float - exclusive gt & lt - valid (below)", &cases.FloatExLTGT{Val: -1}, 0},
	{"float - exclusive gt & lt - invalid", &cases.FloatExLTGT{Val: 5}, 1},
	{"float - exclusive gt & lt - invalid (max)", &cases.FloatExLTGT{Val: 10}, 1},
	{"float - exclusive gt & lt - invalid (min)", &cases.FloatExLTGT{Val: 0}, 1},

	{"float - gte & lte - valid", &cases.FloatGTELTE{Val: 200}, 0},
	{"float - gte & lte - valid (max)", &cases.FloatGTELTE{Val: 256}, 0},
	{"float - gte & lte - valid (min)", &cases.FloatGTELTE{Val: 128}, 0},
	{"float - gte & lte - invalid (above)", &cases.FloatGTELTE{Val: 300}, 1},
	{"float - gte & lte - invalid (below)", &cases.FloatGTELTE{Val: 100}, 1},

	{"float - exclusive gte & lte - valid (above)", &cases.FloatExGTELTE{Val: 300}, 0},
	{"float - exclusive gte & lte - valid (below)", &cases.FloatExGTELTE{Val: 100}, 0},
	{"float - exclusive gte & lte - valid (max)", &cases.FloatExGTELTE{Val: 256}, 0},
	{"float - exclusive gte & lte - valid (min)", &cases.FloatExGTELTE{Val: 128}, 0},
	{"float - exclusive gte & lte - invalid", &cases.FloatExGTELTE{Val: 200}, 1},

	{"float - ignore_empty gte & lte - valid", &cases.FloatIgnore{Val: 0}, 0},
}

var doubleCases = []TestCase{
	{"double - none - valid", &cases.DoubleNone{Val: -1.23456}, 0},

	{"double - const - valid", &cases.DoubleConst{Val: 1.23}, 0},
	{"double - const - invalid", &cases.DoubleConst{Val: 4.56}, 1},

	{"double - in - valid", &cases.DoubleIn{Val: 7.89}, 0},
	{"double - in - invalid", &cases.DoubleIn{Val: 10.11}, 1},

	{"double - not in - valid", &cases.DoubleNotIn{Val: 1}, 0},
	{"double - not in - invalid", &cases.DoubleNotIn{Val: 0}, 1},

	{"double - lt - valid", &cases.DoubleLT{Val: -1}, 0},
	{"double - lt - invalid (equal)", &cases.DoubleLT{Val: 0}, 1},
	{"double - lt - invalid", &cases.DoubleLT{Val: 1}, 1},

	{"double - lte - valid", &cases.DoubleLTE{Val: 63}, 0},
	{"double - lte - valid (equal)", &cases.DoubleLTE{Val: 64}, 0},
	{"double - lte - invalid", &cases.DoubleLTE{Val: 65}, 1},

	{"double - gt - valid", &cases.DoubleGT{Val: 17}, 0},
	{"double - gt - invalid (equal)", &cases.DoubleGT{Val: 16}, 1},
	{"double - gt - invalid", &cases.DoubleGT{Val: 15}, 1},

	{"double - gte - valid", &cases.DoubleGTE{Val: 9}, 0},
	{"double - gte - valid (equal)", &cases.DoubleGTE{Val: 8}, 0},
	{"double - gte - invalid", &cases.DoubleGTE{Val: 7}, 1},

	{"double - gt & lt - valid", &cases.DoubleGTLT{Val: 5}, 0},
	{"double - gt & lt - invalid (above)", &cases.DoubleGTLT{Val: 11}, 1},
	{"double - gt & lt - invalid (below)", &cases.DoubleGTLT{Val: -1}, 1},
	{"double - gt & lt - invalid (max)", &cases.DoubleGTLT{Val: 10}, 1},
	{"double - gt & lt - invalid (min)", &cases.DoubleGTLT{Val: 0}, 1},

	{"double - exclusive gt & lt - valid (above)", &cases.DoubleExLTGT{Val: 11}, 0},
	{"double - exclusive gt & lt - valid (below)", &cases.DoubleExLTGT{Val: -1}, 0},
	{"double - exclusive gt & lt - invalid", &cases.DoubleExLTGT{Val: 5}, 1},
	{"double - exclusive gt & lt - invalid (max)", &cases.DoubleExLTGT{Val: 10}, 1},
	{"double - exclusive gt & lt - invalid (min)", &cases.DoubleExLTGT{Val: 0}, 1},

	{"double - gte & lte - valid", &cases.DoubleGTELTE{Val: 200}, 0},
	{"double - gte & lte - valid (max)", &cases.DoubleGTELTE{Val: 256}, 0},
	{"double - gte & lte - valid (min)", &cases.DoubleGTELTE{Val: 128}, 0},
	{"double - gte & lte - invalid (above)", &cases.DoubleGTELTE{Val: 300}, 1},
	{"double - gte & lte - invalid (below)", &cases.DoubleGTELTE{Val: 100}, 1},

	{"double - exclusive gte & lte - valid (above)", &cases.DoubleExGTELTE{Val: 300}, 0},
	{"double - exclusive gte & lte - valid (below)", &cases.DoubleExGTELTE{Val: 100}, 0},
	{"double - exclusive gte & lte - valid (max)", &cases.DoubleExGTELTE{Val: 256}, 0},
	{"double - exclusive gte & lte - valid (min)", &cases.DoubleExGTELTE{Val: 128}, 0},
	{"double - exclusive gte & lte - invalid", &cases.DoubleExGTELTE{Val: 200}, 1},

	{"double - ignore_empty gte & lte - valid", &cases.DoubleIgnore{Val: 0}, 0},
}

var int32Cases = []TestCase{
	{"int32 - none - valid", &cases.Int32None{Val: 123}, 0},

	{"int32 - const - valid", &cases.Int32Const{Val: 1}, 0},
	{"int32 - const - invalid", &cases.Int32Const{Val: 2}, 1},

	{"int32 - in - valid", &cases.Int32In{Val: 3}, 0},
	{"int32 - in - invalid", &cases.Int32In{Val: 5}, 1},

	{"int32 - not in - valid", &cases.Int32NotIn{Val: 1}, 0},
	{"int32 - not in - invalid", &cases.Int32NotIn{Val: 0}, 1},

	{"int32 - lt - valid", &cases.Int32LT{Val: -1}, 0},
	{"int32 - lt - invalid (equal)", &cases.Int32LT{Val: 0}, 1},
	{"int32 - lt - invalid", &cases.Int32LT{Val: 1}, 1},

	{"int32 - lte - valid", &cases.Int32LTE{Val: 63}, 0},
	{"int32 - lte - valid (equal)", &cases.Int32LTE{Val: 64}, 0},
	{"int32 - lte - invalid", &cases.Int32LTE{Val: 65}, 1},

	{"int32 - gt - valid", &cases.Int32GT{Val: 17}, 0},
	{"int32 - gt - invalid (equal)", &cases.Int32GT{Val: 16}, 1},
	{"int32 - gt - invalid", &cases.Int32GT{Val: 15}, 1},

	{"int32 - gte - valid", &cases.Int32GTE{Val: 9}, 0},
	{"int32 - gte - valid (equal)", &cases.Int32GTE{Val: 8}, 0},
	{"int32 - gte - invalid", &cases.Int32GTE{Val: 7}, 1},

	{"int32 - gt & lt - valid", &cases.Int32GTLT{Val: 5}, 0},
	{"int32 - gt & lt - invalid (above)", &cases.Int32GTLT{Val: 11}, 1},
	{"int32 - gt & lt - invalid (below)", &cases.Int32GTLT{Val: -1}, 1},
	{"int32 - gt & lt - invalid (max)", &cases.Int32GTLT{Val: 10}, 1},
	{"int32 - gt & lt - invalid (min)", &cases.Int32GTLT{Val: 0}, 1},

	{"int32 - exclusive gt & lt - valid (above)", &cases.Int32ExLTGT{Val: 11}, 0},
	{"int32 - exclusive gt & lt - valid (below)", &cases.Int32ExLTGT{Val: -1}, 0},
	{"int32 - exclusive gt & lt - invalid", &cases.Int32ExLTGT{Val: 5}, 1},
	{"int32 - exclusive gt & lt - invalid (max)", &cases.Int32ExLTGT{Val: 10}, 1},
	{"int32 - exclusive gt & lt - invalid (min)", &cases.Int32ExLTGT{Val: 0}, 1},

	{"int32 - gte & lte - valid", &cases.Int32GTELTE{Val: 200}, 0},
	{"int32 - gte & lte - valid (max)", &cases.Int32GTELTE{Val: 256}, 0},
	{"int32 - gte & lte - valid (min)", &cases.Int32GTELTE{Val: 128}, 0},
	{"int32 - gte & lte - invalid (above)", &cases.Int32GTELTE{Val: 300}, 1},
	{"int32 - gte & lte - invalid (below)", &cases.Int32GTELTE{Val: 100}, 1},

	{"int32 - exclusive gte & lte - valid (above)", &cases.Int32ExGTELTE{Val: 300}, 0},
	{"int32 - exclusive gte & lte - valid (below)", &cases.Int32ExGTELTE{Val: 100}, 0},
	{"int32 - exclusive gte & lte - valid (max)", &cases.Int32ExGTELTE{Val: 256}, 0},
	{"int32 - exclusive gte & lte - valid (min)", &cases.Int32ExGTELTE{Val: 128}, 0},
	{"int32 - exclusive gte & lte - invalid", &cases.Int32ExGTELTE{Val: 200}, 1},

	{"int32 - ignore_empty gte & lte - valid", &cases.Int32Ignore{Val: 0}, 0},
}

var int64Cases = []TestCase{
	{"int64 - none - valid", &cases.Int64None{Val: 123}, 0},

	{"int64 - const - valid", &cases.Int64Const{Val: 1}, 0},
	{"int64 - const - invalid", &cases.Int64Const{Val: 2}, 1},

	{"int64 - in - valid", &cases.Int64In{Val: 3}, 0},
	{"int64 - in - invalid", &cases.Int64In{Val: 5}, 1},

	{"int64 - not in - valid", &cases.Int64NotIn{Val: 1}, 0},
	{"int64 - not in - invalid", &cases.Int64NotIn{Val: 0}, 1},

	{"int64 - lt - valid", &cases.Int64LT{Val: -1}, 0},
	{"int64 - lt - invalid (equal)", &cases.Int64LT{Val: 0}, 1},
	{"int64 - lt - invalid", &cases.Int64LT{Val: 1}, 1},

	{"int64 - lte - valid", &cases.Int64LTE{Val: 63}, 0},
	{"int64 - lte - valid (equal)", &cases.Int64LTE{Val: 64}, 0},
	{"int64 - lte - invalid", &cases.Int64LTE{Val: 65}, 1},

	{"int64 - gt - valid", &cases.Int64GT{Val: 17}, 0},
	{"int64 - gt - invalid (equal)", &cases.Int64GT{Val: 16}, 1},
	{"int64 - gt - invalid", &cases.Int64GT{Val: 15}, 1},

	{"int64 - gte - valid", &cases.Int64GTE{Val: 9}, 0},
	{"int64 - gte - valid (equal)", &cases.Int64GTE{Val: 8}, 0},
	{"int64 - gte - invalid", &cases.Int64GTE{Val: 7}, 1},

	{"int64 - gt & lt - valid", &cases.Int64GTLT{Val: 5}, 0},
	{"int64 - gt & lt - invalid (above)", &cases.Int64GTLT{Val: 11}, 1},
	{"int64 - gt & lt - invalid (below)", &cases.Int64GTLT{Val: -1}, 1},
	{"int64 - gt & lt - invalid (max)", &cases.Int64GTLT{Val: 10}, 1},
	{"int64 - gt & lt - invalid (min)", &cases.Int64GTLT{Val: 0}, 1},

	{"int64 - exclusive gt & lt - valid (above)", &cases.Int64ExLTGT{Val: 11}, 0},
	{"int64 - exclusive gt & lt - valid (below)", &cases.Int64ExLTGT{Val: -1}, 0},
	{"int64 - exclusive gt & lt - invalid", &cases.Int64ExLTGT{Val: 5}, 1},
	{"int64 - exclusive gt & lt - invalid (max)", &cases.Int64ExLTGT{Val: 10}, 1},
	{"int64 - exclusive gt & lt - invalid (min)", &cases.Int64ExLTGT{Val: 0}, 1},

	{"int64 - gte & lte - valid", &cases.Int64GTELTE{Val: 200}, 0},
	{"int64 - gte & lte - valid (max)", &cases.Int64GTELTE{Val: 256}, 0},
	{"int64 - gte & lte - valid (min)", &cases.Int64GTELTE{Val: 128}, 0},
	{"int64 - gte & lte - invalid (above)", &cases.Int64GTELTE{Val: 300}, 1},
	{"int64 - gte & lte - invalid (below)", &cases.Int64GTELTE{Val: 100}, 1},

	{"int64 - exclusive gte & lte - valid (above)", &cases.Int64ExGTELTE{Val: 300}, 0},
	{"int64 - exclusive gte & lte - valid (below)", &cases.Int64ExGTELTE{Val: 100}, 0},
	{"int64 - exclusive gte & lte - valid (max)", &cases.Int64ExGTELTE{Val: 256}, 0},
	{"int64 - exclusive gte & lte - valid (min)", &cases.Int64ExGTELTE{Val: 128}, 0},
	{"int64 - exclusive gte & lte - invalid", &cases.Int64ExGTELTE{Val: 200}, 1},

	{"int64 - ignore_empty gte & lte - valid", &cases.Int64Ignore{Val: 0}, 0},

	{"int64 optional - lte - valid", &cases.Int64LTEOptional{Val: &wrapperspb.Int64(63).Value}, 0},
	{"int64 optional - lte - valid (equal)", &cases.Int64LTEOptional{Val: &wrapperspb.Int64(64).Value}, 0},
	{"int64 optional - lte - valid (unset)", &cases.Int64LTEOptional{}, 0},
}

var uint32Cases = []TestCase{
	{"uint32 - none - valid", &cases.UInt32None{Val: 123}, 0},

	{"uint32 - const - valid", &cases.UInt32Const{Val: 1}, 0},
	{"uint32 - const - invalid", &cases.UInt32Const{Val: 2}, 1},

	{"uint32 - in - valid", &cases.UInt32In{Val: 3}, 0},
	{"uint32 - in - invalid", &cases.UInt32In{Val: 5}, 1},

	{"uint32 - not in - valid", &cases.UInt32NotIn{Val: 1}, 0},
	{"uint32 - not in - invalid", &cases.UInt32NotIn{Val: 0}, 1},

	{"uint32 - lt - valid", &cases.UInt32LT{Val: 4}, 0},
	{"uint32 - lt - invalid (equal)", &cases.UInt32LT{Val: 5}, 1},
	{"uint32 - lt - invalid", &cases.UInt32LT{Val: 6}, 1},

	{"uint32 - lte - valid", &cases.UInt32LTE{Val: 63}, 0},
	{"uint32 - lte - valid (equal)", &cases.UInt32LTE{Val: 64}, 0},
	{"uint32 - lte - invalid", &cases.UInt32LTE{Val: 65}, 1},

	{"uint32 - gt - valid", &cases.UInt32GT{Val: 17}, 0},
	{"uint32 - gt - invalid (equal)", &cases.UInt32GT{Val: 16}, 1},
	{"uint32 - gt - invalid", &cases.UInt32GT{Val: 15}, 1},

	{"uint32 - gte - valid", &cases.UInt32GTE{Val: 9}, 0},
	{"uint32 - gte - valid (equal)", &cases.UInt32GTE{Val: 8}, 0},
	{"uint32 - gte - invalid", &cases.UInt32GTE{Val: 7}, 1},

	{"uint32 - gt & lt - valid", &cases.UInt32GTLT{Val: 7}, 0},
	{"uint32 - gt & lt - invalid (above)", &cases.UInt32GTLT{Val: 11}, 1},
	{"uint32 - gt & lt - invalid (below)", &cases.UInt32GTLT{Val: 1}, 1},
	{"uint32 - gt & lt - invalid (max)", &cases.UInt32GTLT{Val: 10}, 1},
	{"uint32 - gt & lt - invalid (min)", &cases.UInt32GTLT{Val: 5}, 1},

	{"uint32 - exclusive gt & lt - valid (above)", &cases.UInt32ExLTGT{Val: 11}, 0},
	{"uint32 - exclusive gt & lt - valid (below)", &cases.UInt32ExLTGT{Val: 4}, 0},
	{"uint32 - exclusive gt & lt - invalid", &cases.UInt32ExLTGT{Val: 7}, 1},
	{"uint32 - exclusive gt & lt - invalid (max)", &cases.UInt32ExLTGT{Val: 10}, 1},
	{"uint32 - exclusive gt & lt - invalid (min)", &cases.UInt32ExLTGT{Val: 5}, 1},

	{"uint32 - gte & lte - valid", &cases.UInt32GTELTE{Val: 200}, 0},
	{"uint32 - gte & lte - valid (max)", &cases.UInt32GTELTE{Val: 256}, 0},
	{"uint32 - gte & lte - valid (min)", &cases.UInt32GTELTE{Val: 128}, 0},
	{"uint32 - gte & lte - invalid (above)", &cases.UInt32GTELTE{Val: 300}, 1},
	{"uint32 - gte & lte - invalid (below)", &cases.UInt32GTELTE{Val: 100}, 1},

	{"uint32 - exclusive gte & lte - valid (above)", &cases.UInt32ExGTELTE{Val: 300}, 0},
	{"uint32 - exclusive gte & lte - valid (below)", &cases.UInt32ExGTELTE{Val: 100}, 0},
	{"uint32 - exclusive gte & lte - valid (max)", &cases.UInt32ExGTELTE{Val: 256}, 0},
	{"uint32 - exclusive gte & lte - valid (min)", &cases.UInt32ExGTELTE{Val: 128}, 0},
	{"uint32 - exclusive gte & lte - invalid", &cases.UInt32ExGTELTE{Val: 200}, 1},

	{"uint32 - ignore_empty gte & lte - valid", &cases.UInt32Ignore{Val: 0}, 0},
}

var uint64Cases = []TestCase{
	{"uint64 - none - valid", &cases.UInt64None{Val: 123}, 0},

	{"uint64 - const - valid", &cases.UInt64Const{Val: 1}, 0},
	{"uint64 - const - invalid", &cases.UInt64Const{Val: 2}, 1},

	{"uint64 - in - valid", &cases.UInt64In{Val: 3}, 0},
	{"uint64 - in - invalid", &cases.UInt64In{Val: 5}, 1},

	{"uint64 - not in - valid", &cases.UInt64NotIn{Val: 1}, 0},
	{"uint64 - not in - invalid", &cases.UInt64NotIn{Val: 0}, 1},

	{"uint64 - lt - valid", &cases.UInt64LT{Val: 4}, 0},
	{"uint64 - lt - invalid (equal)", &cases.UInt64LT{Val: 5}, 1},
	{"uint64 - lt - invalid", &cases.UInt64LT{Val: 6}, 1},

	{"uint64 - lte - valid", &cases.UInt64LTE{Val: 63}, 0},
	{"uint64 - lte - valid (equal)", &cases.UInt64LTE{Val: 64}, 0},
	{"uint64 - lte - invalid", &cases.UInt64LTE{Val: 65}, 1},

	{"uint64 - gt - valid", &cases.UInt64GT{Val: 17}, 0},
	{"uint64 - gt - invalid (equal)", &cases.UInt64GT{Val: 16}, 1},
	{"uint64 - gt - invalid", &cases.UInt64GT{Val: 15}, 1},

	{"uint64 - gte - valid", &cases.UInt64GTE{Val: 9}, 0},
	{"uint64 - gte - valid (equal)", &cases.UInt64GTE{Val: 8}, 0},
	{"uint64 - gte - invalid", &cases.UInt64GTE{Val: 7}, 1},

	{"uint64 - gt & lt - valid", &cases.UInt64GTLT{Val: 7}, 0},
	{"uint64 - gt & lt - invalid (above)", &cases.UInt64GTLT{Val: 11}, 1},
	{"uint64 - gt & lt - invalid (below)", &cases.UInt64GTLT{Val: 1}, 1},
	{"uint64 - gt & lt - invalid (max)", &cases.UInt64GTLT{Val: 10}, 1},
	{"uint64 - gt & lt - invalid (min)", &cases.UInt64GTLT{Val: 5}, 1},

	{"uint64 - exclusive gt & lt - valid (above)", &cases.UInt64ExLTGT{Val: 11}, 0},
	{"uint64 - exclusive gt & lt - valid (below)", &cases.UInt64ExLTGT{Val: 4}, 0},
	{"uint64 - exclusive gt & lt - invalid", &cases.UInt64ExLTGT{Val: 7}, 1},
	{"uint64 - exclusive gt & lt - invalid (max)", &cases.UInt64ExLTGT{Val: 10}, 1},
	{"uint64 - exclusive gt & lt - invalid (min)", &cases.UInt64ExLTGT{Val: 5}, 1},

	{"uint64 - gte & lte - valid", &cases.UInt64GTELTE{Val: 200}, 0},
	{"uint64 - gte & lte - valid (max)", &cases.UInt64GTELTE{Val: 256}, 0},
	{"uint64 - gte & lte - valid (min)", &cases.UInt64GTELTE{Val: 128}, 0},
	{"uint64 - gte & lte - invalid (above)", &cases.UInt64GTELTE{Val: 300}, 1},
	{"uint64 - gte & lte - invalid (below)", &cases.UInt64GTELTE{Val: 100}, 1},

	{"uint64 - exclusive gte & lte - valid (above)", &cases.UInt64ExGTELTE{Val: 300}, 0},
	{"uint64 - exclusive gte & lte - valid (below)", &cases.UInt64ExGTELTE{Val: 100}, 0},
	{"uint64 - exclusive gte & lte - valid (max)", &cases.UInt64ExGTELTE{Val: 256}, 0},
	{"uint64 - exclusive gte & lte - valid (min)", &cases.UInt64ExGTELTE{Val: 128}, 0},
	{"uint64 - exclusive gte & lte - invalid", &cases.UInt64ExGTELTE{Val: 200}, 1},

	{"uint64 - ignore_empty gte & lte - valid", &cases.UInt64Ignore{Val: 0}, 0},
}

var sint32Cases = []TestCase{
	{"sint32 - none - valid", &cases.SInt32None{Val: 123}, 0},

	{"sint32 - const - valid", &cases.SInt32Const{Val: 1}, 0},
	{"sint32 - const - invalid", &cases.SInt32Const{Val: 2}, 1},

	{"sint32 - in - valid", &cases.SInt32In{Val: 3}, 0},
	{"sint32 - in - invalid", &cases.SInt32In{Val: 5}, 1},

	{"sint32 - not in - valid", &cases.SInt32NotIn{Val: 1}, 0},
	{"sint32 - not in - invalid", &cases.SInt32NotIn{Val: 0}, 1},

	{"sint32 - lt - valid", &cases.SInt32LT{Val: -1}, 0},
	{"sint32 - lt - invalid (equal)", &cases.SInt32LT{Val: 0}, 1},
	{"sint32 - lt - invalid", &cases.SInt32LT{Val: 1}, 1},

	{"sint32 - lte - valid", &cases.SInt32LTE{Val: 63}, 0},
	{"sint32 - lte - valid (equal)", &cases.SInt32LTE{Val: 64}, 0},
	{"sint32 - lte - invalid", &cases.SInt32LTE{Val: 65}, 1},

	{"sint32 - gt - valid", &cases.SInt32GT{Val: 17}, 0},
	{"sint32 - gt - invalid (equal)", &cases.SInt32GT{Val: 16}, 1},
	{"sint32 - gt - invalid", &cases.SInt32GT{Val: 15}, 1},

	{"sint32 - gte - valid", &cases.SInt32GTE{Val: 9}, 0},
	{"sint32 - gte - valid (equal)", &cases.SInt32GTE{Val: 8}, 0},
	{"sint32 - gte - invalid", &cases.SInt32GTE{Val: 7}, 1},

	{"sint32 - gt & lt - valid", &cases.SInt32GTLT{Val: 5}, 0},
	{"sint32 - gt & lt - invalid (above)", &cases.SInt32GTLT{Val: 11}, 1},
	{"sint32 - gt & lt - invalid (below)", &cases.SInt32GTLT{Val: -1}, 1},
	{"sint32 - gt & lt - invalid (max)", &cases.SInt32GTLT{Val: 10}, 1},
	{"sint32 - gt & lt - invalid (min)", &cases.SInt32GTLT{Val: 0}, 1},

	{"sint32 - exclusive gt & lt - valid (above)", &cases.SInt32ExLTGT{Val: 11}, 0},
	{"sint32 - exclusive gt & lt - valid (below)", &cases.SInt32ExLTGT{Val: -1}, 0},
	{"sint32 - exclusive gt & lt - invalid", &cases.SInt32ExLTGT{Val: 5}, 1},
	{"sint32 - exclusive gt & lt - invalid (max)", &cases.SInt32ExLTGT{Val: 10}, 1},
	{"sint32 - exclusive gt & lt - invalid (min)", &cases.SInt32ExLTGT{Val: 0}, 1},

	{"sint32 - gte & lte - valid", &cases.SInt32GTELTE{Val: 200}, 0},
	{"sint32 - gte & lte - valid (max)", &cases.SInt32GTELTE{Val: 256}, 0},
	{"sint32 - gte & lte - valid (min)", &cases.SInt32GTELTE{Val: 128}, 0},
	{"sint32 - gte & lte - invalid (above)", &cases.SInt32GTELTE{Val: 300}, 1},
	{"sint32 - gte & lte - invalid (below)", &cases.SInt32GTELTE{Val: 100}, 1},

	{"sint32 - exclusive gte & lte - valid (above)", &cases.SInt32ExGTELTE{Val: 300}, 0},
	{"sint32 - exclusive gte & lte - valid (below)", &cases.SInt32ExGTELTE{Val: 100}, 0},
	{"sint32 - exclusive gte & lte - valid (max)", &cases.SInt32ExGTELTE{Val: 256}, 0},
	{"sint32 - exclusive gte & lte - valid (min)", &cases.SInt32ExGTELTE{Val: 128}, 0},
	{"sint32 - exclusive gte & lte - invalid", &cases.SInt32ExGTELTE{Val: 200}, 1},

	{"sint32 - ignore_empty gte & lte - valid", &cases.SInt32Ignore{Val: 0}, 0},
}

var sint64Cases = []TestCase{
	{"sint64 - none - valid", &cases.SInt64None{Val: 123}, 0},

	{"sint64 - const - valid", &cases.SInt64Const{Val: 1}, 0},
	{"sint64 - const - invalid", &cases.SInt64Const{Val: 2}, 1},

	{"sint64 - in - valid", &cases.SInt64In{Val: 3}, 0},
	{"sint64 - in - invalid", &cases.SInt64In{Val: 5}, 1},

	{"sint64 - not in - valid", &cases.SInt64NotIn{Val: 1}, 0},
	{"sint64 - not in - invalid", &cases.SInt64NotIn{Val: 0}, 1},

	{"sint64 - lt - valid", &cases.SInt64LT{Val: -1}, 0},
	{"sint64 - lt - invalid (equal)", &cases.SInt64LT{Val: 0}, 1},
	{"sint64 - lt - invalid", &cases.SInt64LT{Val: 1}, 1},

	{"sint64 - lte - valid", &cases.SInt64LTE{Val: 63}, 0},
	{"sint64 - lte - valid (equal)", &cases.SInt64LTE{Val: 64}, 0},
	{"sint64 - lte - invalid", &cases.SInt64LTE{Val: 65}, 1},

	{"sint64 - gt - valid", &cases.SInt64GT{Val: 17}, 0},
	{"sint64 - gt - invalid (equal)", &cases.SInt64GT{Val: 16}, 1},
	{"sint64 - gt - invalid", &cases.SInt64GT{Val: 15}, 1},

	{"sint64 - gte - valid", &cases.SInt64GTE{Val: 9}, 0},
	{"sint64 - gte - valid (equal)", &cases.SInt64GTE{Val: 8}, 0},
	{"sint64 - gte - invalid", &cases.SInt64GTE{Val: 7}, 1},

	{"sint64 - gt & lt - valid", &cases.SInt64GTLT{Val: 5}, 0},
	{"sint64 - gt & lt - invalid (above)", &cases.SInt64GTLT{Val: 11}, 1},
	{"sint64 - gt & lt - invalid (below)", &cases.SInt64GTLT{Val: -1}, 1},
	{"sint64 - gt & lt - invalid (max)", &cases.SInt64GTLT{Val: 10}, 1},
	{"sint64 - gt & lt - invalid (min)", &cases.SInt64GTLT{Val: 0}, 1},

	{"sint64 - exclusive gt & lt - valid (above)", &cases.SInt64ExLTGT{Val: 11}, 0},
	{"sint64 - exclusive gt & lt - valid (below)", &cases.SInt64ExLTGT{Val: -1}, 0},
	{"sint64 - exclusive gt & lt - invalid", &cases.SInt64ExLTGT{Val: 5}, 1},
	{"sint64 - exclusive gt & lt - invalid (max)", &cases.SInt64ExLTGT{Val: 10}, 1},
	{"sint64 - exclusive gt & lt - invalid (min)", &cases.SInt64ExLTGT{Val: 0}, 1},

	{"sint64 - gte & lte - valid", &cases.SInt64GTELTE{Val: 200}, 0},
	{"sint64 - gte & lte - valid (max)", &cases.SInt64GTELTE{Val: 256}, 0},
	{"sint64 - gte & lte - valid (min)", &cases.SInt64GTELTE{Val: 128}, 0},
	{"sint64 - gte & lte - invalid (above)", &cases.SInt64GTELTE{Val: 300}, 1},
	{"sint64 - gte & lte - invalid (below)", &cases.SInt64GTELTE{Val: 100}, 1},

	{"sint64 - exclusive gte & lte - valid (above)", &cases.SInt64ExGTELTE{Val: 300}, 0},
	{"sint64 - exclusive gte & lte - valid (below)", &cases.SInt64ExGTELTE{Val: 100}, 0},
	{"sint64 - exclusive gte & lte - valid (max)", &cases.SInt64ExGTELTE{Val: 256}, 0},
	{"sint64 - exclusive gte & lte - valid (min)", &cases.SInt64ExGTELTE{Val: 128}, 0},
	{"sint64 - exclusive gte & lte - invalid", &cases.SInt64ExGTELTE{Val: 200}, 1},

	{"sint64 - ignore_empty gte & lte - valid", &cases.SInt64Ignore{Val: 0}, 0},
}

var fixed32Cases = []TestCase{
	{"fixed32 - none - valid", &cases.Fixed32None{Val: 123}, 0},

	{"fixed32 - const - valid", &cases.Fixed32Const{Val: 1}, 0},
	{"fixed32 - const - invalid", &cases.Fixed32Const{Val: 2}, 1},

	{"fixed32 - in - valid", &cases.Fixed32In{Val: 3}, 0},
	{"fixed32 - in - invalid", &cases.Fixed32In{Val: 5}, 1},

	{"fixed32 - not in - valid", &cases.Fixed32NotIn{Val: 1}, 0},
	{"fixed32 - not in - invalid", &cases.Fixed32NotIn{Val: 0}, 1},

	{"fixed32 - lt - valid", &cases.Fixed32LT{Val: 4}, 0},
	{"fixed32 - lt - invalid (equal)", &cases.Fixed32LT{Val: 5}, 1},
	{"fixed32 - lt - invalid", &cases.Fixed32LT{Val: 6}, 1},

	{"fixed32 - lte - valid", &cases.Fixed32LTE{Val: 63}, 0},
	{"fixed32 - lte - valid (equal)", &cases.Fixed32LTE{Val: 64}, 0},
	{"fixed32 - lte - invalid", &cases.Fixed32LTE{Val: 65}, 1},

	{"fixed32 - gt - valid", &cases.Fixed32GT{Val: 17}, 0},
	{"fixed32 - gt - invalid (equal)", &cases.Fixed32GT{Val: 16}, 1},
	{"fixed32 - gt - invalid", &cases.Fixed32GT{Val: 15}, 1},

	{"fixed32 - gte - valid", &cases.Fixed32GTE{Val: 9}, 0},
	{"fixed32 - gte - valid (equal)", &cases.Fixed32GTE{Val: 8}, 0},
	{"fixed32 - gte - invalid", &cases.Fixed32GTE{Val: 7}, 1},

	{"fixed32 - gt & lt - valid", &cases.Fixed32GTLT{Val: 7}, 0},
	{"fixed32 - gt & lt - invalid (above)", &cases.Fixed32GTLT{Val: 11}, 1},
	{"fixed32 - gt & lt - invalid (below)", &cases.Fixed32GTLT{Val: 1}, 1},
	{"fixed32 - gt & lt - invalid (max)", &cases.Fixed32GTLT{Val: 10}, 1},
	{"fixed32 - gt & lt - invalid (min)", &cases.Fixed32GTLT{Val: 5}, 1},

	{"fixed32 - exclusive gt & lt - valid (above)", &cases.Fixed32ExLTGT{Val: 11}, 0},
	{"fixed32 - exclusive gt & lt - valid (below)", &cases.Fixed32ExLTGT{Val: 4}, 0},
	{"fixed32 - exclusive gt & lt - invalid", &cases.Fixed32ExLTGT{Val: 7}, 1},
	{"fixed32 - exclusive gt & lt - invalid (max)", &cases.Fixed32ExLTGT{Val: 10}, 1},
	{"fixed32 - exclusive gt & lt - invalid (min)", &cases.Fixed32ExLTGT{Val: 5}, 1},

	{"fixed32 - gte & lte - valid", &cases.Fixed32GTELTE{Val: 200}, 0},
	{"fixed32 - gte & lte - valid (max)", &cases.Fixed32GTELTE{Val: 256}, 0},
	{"fixed32 - gte & lte - valid (min)", &cases.Fixed32GTELTE{Val: 128}, 0},
	{"fixed32 - gte & lte - invalid (above)", &cases.Fixed32GTELTE{Val: 300}, 1},
	{"fixed32 - gte & lte - invalid (below)", &cases.Fixed32GTELTE{Val: 100}, 1},

	{"fixed32 - exclusive gte & lte - valid (above)", &cases.Fixed32ExGTELTE{Val: 300}, 0},
	{"fixed32 - exclusive gte & lte - valid (below)", &cases.Fixed32ExGTELTE{Val: 100}, 0},
	{"fixed32 - exclusive gte & lte - valid (max)", &cases.Fixed32ExGTELTE{Val: 256}, 0},
	{"fixed32 - exclusive gte & lte - valid (min)", &cases.Fixed32ExGTELTE{Val: 128}, 0},
	{"fixed32 - exclusive gte & lte - invalid", &cases.Fixed32ExGTELTE{Val: 200}, 1},

	{"fixed32 - ignore_empty gte & lte - valid", &cases.Fixed32Ignore{Val: 0}, 0},
}

var fixed64Cases = []TestCase{
	{"fixed64 - none - valid", &cases.Fixed64None{Val: 123}, 0},

	{"fixed64 - const - valid", &cases.Fixed64Const{Val: 1}, 0},
	{"fixed64 - const - invalid", &cases.Fixed64Const{Val: 2}, 1},

	{"fixed64 - in - valid", &cases.Fixed64In{Val: 3}, 0},
	{"fixed64 - in - invalid", &cases.Fixed64In{Val: 5}, 1},

	{"fixed64 - not in - valid", &cases.Fixed64NotIn{Val: 1}, 0},
	{"fixed64 - not in - invalid", &cases.Fixed64NotIn{Val: 0}, 1},

	{"fixed64 - lt - valid", &cases.Fixed64LT{Val: 4}, 0},
	{"fixed64 - lt - invalid (equal)", &cases.Fixed64LT{Val: 5}, 1},
	{"fixed64 - lt - invalid", &cases.Fixed64LT{Val: 6}, 1},

	{"fixed64 - lte - valid", &cases.Fixed64LTE{Val: 63}, 0},
	{"fixed64 - lte - valid (equal)", &cases.Fixed64LTE{Val: 64}, 0},
	{"fixed64 - lte - invalid", &cases.Fixed64LTE{Val: 65}, 1},

	{"fixed64 - gt - valid", &cases.Fixed64GT{Val: 17}, 0},
	{"fixed64 - gt - invalid (equal)", &cases.Fixed64GT{Val: 16}, 1},
	{"fixed64 - gt - invalid", &cases.Fixed64GT{Val: 15}, 1},

	{"fixed64 - gte - valid", &cases.Fixed64GTE{Val: 9}, 0},
	{"fixed64 - gte - valid (equal)", &cases.Fixed64GTE{Val: 8}, 0},
	{"fixed64 - gte - invalid", &cases.Fixed64GTE{Val: 7}, 1},

	{"fixed64 - gt & lt - valid", &cases.Fixed64GTLT{Val: 7}, 0},
	{"fixed64 - gt & lt - invalid (above)", &cases.Fixed64GTLT{Val: 11}, 1},
	{"fixed64 - gt & lt - invalid (below)", &cases.Fixed64GTLT{Val: 1}, 1},
	{"fixed64 - gt & lt - invalid (max)", &cases.Fixed64GTLT{Val: 10}, 1},
	{"fixed64 - gt & lt - invalid (min)", &cases.Fixed64GTLT{Val: 5}, 1},

	{"fixed64 - exclusive gt & lt - valid (above)", &cases.Fixed64ExLTGT{Val: 11}, 0},
	{"fixed64 - exclusive gt & lt - valid (below)", &cases.Fixed64ExLTGT{Val: 4}, 0},
	{"fixed64 - exclusive gt & lt - invalid", &cases.Fixed64ExLTGT{Val: 7}, 1},
	{"fixed64 - exclusive gt & lt - invalid (max)", &cases.Fixed64ExLTGT{Val: 10}, 1},
	{"fixed64 - exclusive gt & lt - invalid (min)", &cases.Fixed64ExLTGT{Val: 5}, 1},

	{"fixed64 - gte & lte - valid", &cases.Fixed64GTELTE{Val: 200}, 0},
	{"fixed64 - gte & lte - valid (max)", &cases.Fixed64GTELTE{Val: 256}, 0},
	{"fixed64 - gte & lte - valid (min)", &cases.Fixed64GTELTE{Val: 128}, 0},
	{"fixed64 - gte & lte - invalid (above)", &cases.Fixed64GTELTE{Val: 300}, 1},
	{"fixed64 - gte & lte - invalid (below)", &cases.Fixed64GTELTE{Val: 100}, 1},

	{"fixed64 - exclusive gte & lte - valid (above)", &cases.Fixed64ExGTELTE{Val: 300}, 0},
	{"fixed64 - exclusive gte & lte - valid (below)", &cases.Fixed64ExGTELTE{Val: 100}, 0},
	{"fixed64 - exclusive gte & lte - valid (max)", &cases.Fixed64ExGTELTE{Val: 256}, 0},
	{"fixed64 - exclusive gte & lte - valid (min)", &cases.Fixed64ExGTELTE{Val: 128}, 0},
	{"fixed64 - exclusive gte & lte - invalid", &cases.Fixed64ExGTELTE{Val: 200}, 1},

	{"fixed64 - ignore_empty gte & lte - valid", &cases.Fixed64Ignore{Val: 0}, 0},
}

var sfixed32Cases = []TestCase{
	{"sfixed32 - none - valid", &cases.SFixed32None{Val: 123}, 0},

	{"sfixed32 - const - valid", &cases.SFixed32Const{Val: 1}, 0},
	{"sfixed32 - const - invalid", &cases.SFixed32Const{Val: 2}, 1},

	{"sfixed32 - in - valid", &cases.SFixed32In{Val: 3}, 0},
	{"sfixed32 - in - invalid", &cases.SFixed32In{Val: 5}, 1},

	{"sfixed32 - not in - valid", &cases.SFixed32NotIn{Val: 1}, 0},
	{"sfixed32 - not in - invalid", &cases.SFixed32NotIn{Val: 0}, 1},

	{"sfixed32 - lt - valid", &cases.SFixed32LT{Val: -1}, 0},
	{"sfixed32 - lt - invalid (equal)", &cases.SFixed32LT{Val: 0}, 1},
	{"sfixed32 - lt - invalid", &cases.SFixed32LT{Val: 1}, 1},

	{"sfixed32 - lte - valid", &cases.SFixed32LTE{Val: 63}, 0},
	{"sfixed32 - lte - valid (equal)", &cases.SFixed32LTE{Val: 64}, 0},
	{"sfixed32 - lte - invalid", &cases.SFixed32LTE{Val: 65}, 1},

	{"sfixed32 - gt - valid", &cases.SFixed32GT{Val: 17}, 0},
	{"sfixed32 - gt - invalid (equal)", &cases.SFixed32GT{Val: 16}, 1},
	{"sfixed32 - gt - invalid", &cases.SFixed32GT{Val: 15}, 1},

	{"sfixed32 - gte - valid", &cases.SFixed32GTE{Val: 9}, 0},
	{"sfixed32 - gte - valid (equal)", &cases.SFixed32GTE{Val: 8}, 0},
	{"sfixed32 - gte - invalid", &cases.SFixed32GTE{Val: 7}, 1},

	{"sfixed32 - gt & lt - valid", &cases.SFixed32GTLT{Val: 5}, 0},
	{"sfixed32 - gt & lt - invalid (above)", &cases.SFixed32GTLT{Val: 11}, 1},
	{"sfixed32 - gt & lt - invalid (below)", &cases.SFixed32GTLT{Val: -1}, 1},
	{"sfixed32 - gt & lt - invalid (max)", &cases.SFixed32GTLT{Val: 10}, 1},
	{"sfixed32 - gt & lt - invalid (min)", &cases.SFixed32GTLT{Val: 0}, 1},

	{"sfixed32 - exclusive gt & lt - valid (above)", &cases.SFixed32ExLTGT{Val: 11}, 0},
	{"sfixed32 - exclusive gt & lt - valid (below)", &cases.SFixed32ExLTGT{Val: -1}, 0},
	{"sfixed32 - exclusive gt & lt - invalid", &cases.SFixed32ExLTGT{Val: 5}, 1},
	{"sfixed32 - exclusive gt & lt - invalid (max)", &cases.SFixed32ExLTGT{Val: 10}, 1},
	{"sfixed32 - exclusive gt & lt - invalid (min)", &cases.SFixed32ExLTGT{Val: 0}, 1},

	{"sfixed32 - gte & lte - valid", &cases.SFixed32GTELTE{Val: 200}, 0},
	{"sfixed32 - gte & lte - valid (max)", &cases.SFixed32GTELTE{Val: 256}, 0},
	{"sfixed32 - gte & lte - valid (min)", &cases.SFixed32GTELTE{Val: 128}, 0},
	{"sfixed32 - gte & lte - invalid (above)", &cases.SFixed32GTELTE{Val: 300}, 1},
	{"sfixed32 - gte & lte - invalid (below)", &cases.SFixed32GTELTE{Val: 100}, 1},

	{"sfixed32 - exclusive gte & lte - valid (above)", &cases.SFixed32ExGTELTE{Val: 300}, 0},
	{"sfixed32 - exclusive gte & lte - valid (below)", &cases.SFixed32ExGTELTE{Val: 100}, 0},
	{"sfixed32 - exclusive gte & lte - valid (max)", &cases.SFixed32ExGTELTE{Val: 256}, 0},
	{"sfixed32 - exclusive gte & lte - valid (min)", &cases.SFixed32ExGTELTE{Val: 128}, 0},
	{"sfixed32 - exclusive gte & lte - invalid", &cases.SFixed32ExGTELTE{Val: 200}, 1},

	{"sfixed32 - ignore_empty gte & lte - valid", &cases.SFixed32Ignore{Val: 0}, 0},
}

var sfixed64Cases = []TestCase{
	{"sfixed64 - none - valid", &cases.SFixed64None{Val: 123}, 0},

	{"sfixed64 - const - valid", &cases.SFixed64Const{Val: 1}, 0},
	{"sfixed64 - const - invalid", &cases.SFixed64Const{Val: 2}, 1},

	{"sfixed64 - in - valid", &cases.SFixed64In{Val: 3}, 0},
	{"sfixed64 - in - invalid", &cases.SFixed64In{Val: 5}, 1},

	{"sfixed64 - not in - valid", &cases.SFixed64NotIn{Val: 1}, 0},
	{"sfixed64 - not in - invalid", &cases.SFixed64NotIn{Val: 0}, 1},

	{"sfixed64 - lt - valid", &cases.SFixed64LT{Val: -1}, 0},
	{"sfixed64 - lt - invalid (equal)", &cases.SFixed64LT{Val: 0}, 1},
	{"sfixed64 - lt - invalid", &cases.SFixed64LT{Val: 1}, 1},

	{"sfixed64 - lte - valid", &cases.SFixed64LTE{Val: 63}, 0},
	{"sfixed64 - lte - valid (equal)", &cases.SFixed64LTE{Val: 64}, 0},
	{"sfixed64 - lte - invalid", &cases.SFixed64LTE{Val: 65}, 1},

	{"sfixed64 - gt - valid", &cases.SFixed64GT{Val: 17}, 0},
	{"sfixed64 - gt - invalid (equal)", &cases.SFixed64GT{Val: 16}, 1},
	{"sfixed64 - gt - invalid", &cases.SFixed64GT{Val: 15}, 1},

	{"sfixed64 - gte - valid", &cases.SFixed64GTE{Val: 9}, 0},
	{"sfixed64 - gte - valid (equal)", &cases.SFixed64GTE{Val: 8}, 0},
	{"sfixed64 - gte - invalid", &cases.SFixed64GTE{Val: 7}, 1},

	{"sfixed64 - gt & lt - valid", &cases.SFixed64GTLT{Val: 5}, 0},
	{"sfixed64 - gt & lt - invalid (above)", &cases.SFixed64GTLT{Val: 11}, 1},
	{"sfixed64 - gt & lt - invalid (below)", &cases.SFixed64GTLT{Val: -1}, 1},
	{"sfixed64 - gt & lt - invalid (max)", &cases.SFixed64GTLT{Val: 10}, 1},
	{"sfixed64 - gt & lt - invalid (min)", &cases.SFixed64GTLT{Val: 0}, 1},

	{"sfixed64 - exclusive gt & lt - valid (above)", &cases.SFixed64ExLTGT{Val: 11}, 0},
	{"sfixed64 - exclusive gt & lt - valid (below)", &cases.SFixed64ExLTGT{Val: -1}, 0},
	{"sfixed64 - exclusive gt & lt - invalid", &cases.SFixed64ExLTGT{Val: 5}, 1},
	{"sfixed64 - exclusive gt & lt - invalid (max)", &cases.SFixed64ExLTGT{Val: 10}, 1},
	{"sfixed64 - exclusive gt & lt - invalid (min)", &cases.SFixed64ExLTGT{Val: 0}, 1},

	{"sfixed64 - gte & lte - valid", &cases.SFixed64GTELTE{Val: 200}, 0},
	{"sfixed64 - gte & lte - valid (max)", &cases.SFixed64GTELTE{Val: 256}, 0},
	{"sfixed64 - gte & lte - valid (min)", &cases.SFixed64GTELTE{Val: 128}, 0},
	{"sfixed64 - gte & lte - invalid (above)", &cases.SFixed64GTELTE{Val: 300}, 1},
	{"sfixed64 - gte & lte - invalid (below)", &cases.SFixed64GTELTE{Val: 100}, 1},

	{"sfixed64 - exclusive gte & lte - valid (above)", &cases.SFixed64ExGTELTE{Val: 300}, 0},
	{"sfixed64 - exclusive gte & lte - valid (below)", &cases.SFixed64ExGTELTE{Val: 100}, 0},
	{"sfixed64 - exclusive gte & lte - valid (max)", &cases.SFixed64ExGTELTE{Val: 256}, 0},
	{"sfixed64 - exclusive gte & lte - valid (min)", &cases.SFixed64ExGTELTE{Val: 128}, 0},
	{"sfixed64 - exclusive gte & lte - invalid", &cases.SFixed64ExGTELTE{Val: 200}, 1},

	{"sfixed64 - ignore_empty gte & lte - valid", &cases.SFixed64Ignore{Val: 0}, 0},
}

var boolCases = []TestCase{
	{"bool - none - valid", &cases.BoolNone{Val: true}, 0},
	{"bool - const (true) - valid", &cases.BoolConstTrue{Val: true}, 0},
	{"bool - const (true) - invalid", &cases.BoolConstTrue{Val: false}, 1},
	{"bool - const (false) - valid", &cases.BoolConstFalse{Val: false}, 0},
	{"bool - const (false) - invalid", &cases.BoolConstFalse{Val: true}, 1},
}

var stringCases = []TestCase{
	{"string - none - valid", &cases.StringNone{Val: "quux"}, 0},

	{"string - const - valid", &cases.StringConst{Val: "foo"}, 0},
	{"string - const - invalid", &cases.StringConst{Val: "bar"}, 1},

	{"string - in - valid", &cases.StringIn{Val: "bar"}, 0},
	{"string - in - invalid", &cases.StringIn{Val: "quux"}, 1},
	{"string - not in - valid", &cases.StringNotIn{Val: "quux"}, 0},
	{"string - not in - invalid", &cases.StringNotIn{Val: "fizz"}, 1},

	{"string - len - valid", &cases.StringLen{Val: "baz"}, 0},
	{"string - len - valid (multibyte)", &cases.StringLen{Val: "你好吖"}, 0},
	{"string - len - invalid (lt)", &cases.StringLen{Val: "go"}, 1},
	{"string - len - invalid (gt)", &cases.StringLen{Val: "fizz"}, 1},
	{"string - len - invalid (multibyte)", &cases.StringLen{Val: "你好"}, 1},

	{"string - min len - valid", &cases.StringMinLen{Val: "protoc"}, 0},
	{"string - min len - valid (min)", &cases.StringMinLen{Val: "baz"}, 0},
	{"string - min len - invalid", &cases.StringMinLen{Val: "go"}, 1},
	{"string - min len - invalid (multibyte)", &cases.StringMinLen{Val: "你好"}, 1},

	{"string - max len - valid", &cases.StringMaxLen{Val: "foo"}, 0},
	{"string - max len - valid (max)", &cases.StringMaxLen{Val: "proto"}, 0},
	{"string - max len - valid (multibyte)", &cases.StringMaxLen{Val: "你好你好"}, 0},
	{"string - max len - invalid", &cases.StringMaxLen{Val: "1234567890"}, 1},

	{"string - min/max len - valid", &cases.StringMinMaxLen{Val: "quux"}, 0},
	{"string - min/max len - valid (min)", &cases.StringMinMaxLen{Val: "foo"}, 0},
	{"string - min/max len - valid (max)", &cases.StringMinMaxLen{Val: "proto"}, 0},
	{"string - min/max len - valid (multibyte)", &cases.StringMinMaxLen{Val: "你好你好"}, 0},
	{"string - min/max len - invalid (below)", &cases.StringMinMaxLen{Val: "go"}, 1},
	{"string - min/max len - invalid (above)", &cases.StringMinMaxLen{Val: "validate"}, 1},

	{"string - equal min/max len - valid", &cases.StringEqualMinMaxLen{Val: "proto"}, 0},
	{"string - equal min/max len - invalid", &cases.StringEqualMinMaxLen{Val: "validate"}, 1},

	{"string - len bytes - valid", &cases.StringLenBytes{Val: "pace"}, 0},
	{"string - len bytes - invalid (lt)", &cases.StringLenBytes{Val: "val"}, 1},
	{"string - len bytes - invalid (gt)", &cases.StringLenBytes{Val: "world"}, 1},
	{"string - len bytes - invalid (multibyte)", &cases.StringLenBytes{Val: "世界和平"}, 1},

	{"string - min bytes - valid", &cases.StringMinBytes{Val: "proto"}, 0},
	{"string - min bytes - valid (min)", &cases.StringMinBytes{Val: "quux"}, 0},
	{"string - min bytes - valid (multibyte)", &cases.StringMinBytes{Val: "你好"}, 0},
	{"string - min bytes - invalid", &cases.StringMinBytes{Val: ""}, 1},

	{"string - max bytes - valid", &cases.StringMaxBytes{Val: "foo"}, 0},
	{"string - max bytes - valid (max)", &cases.StringMaxBytes{Val: "12345678"}, 0},
	{"string - max bytes - invalid", &cases.StringMaxBytes{Val: "123456789"}, 1},
	{"string - max bytes - invalid (multibyte)", &cases.StringMaxBytes{Val: "你好你好你好"}, 1},

	{"string - min/max bytes - valid", &cases.StringMinMaxBytes{Val: "protoc"}, 0},
	{"string - min/max bytes - valid (min)", &cases.StringMinMaxBytes{Val: "quux"}, 0},
	{"string - min/max bytes - valid (max)", &cases.StringMinMaxBytes{Val: "fizzbuzz"}, 0},
	{"string - min/max bytes - valid (multibyte)", &cases.StringMinMaxBytes{Val: "你好"}, 0},
	{"string - min/max bytes - invalid (below)", &cases.StringMinMaxBytes{Val: "foo"}, 1},
	{"string - min/max bytes - invalid (above)", &cases.StringMinMaxBytes{Val: "你好你好你"}, 1},

	{"string - equal min/max bytes - valid", &cases.StringEqualMinMaxBytes{Val: "protoc"}, 0},
	{"string - equal min/max bytes - invalid", &cases.StringEqualMinMaxBytes{Val: "foo"}, 1},

	{"string - pattern - valid", &cases.StringPattern{Val: "Foo123"}, 0},
	{"string - pattern - invalid", &cases.StringPattern{Val: "!@#$%^&*()"}, 1},
	{"string - pattern - invalid (empty)", &cases.StringPattern{Val: ""}, 1},
	{"string - pattern - invalid (null)", &cases.StringPattern{Val: "a\000"}, 1},

	{"string - pattern (escapes) - valid", &cases.StringPatternEscapes{Val: "* \\ x"}, 0},
	{"string - pattern (escapes) - invalid", &cases.StringPatternEscapes{Val: "invalid"}, 1},
	{"string - pattern (escapes) - invalid (empty)", &cases.StringPatternEscapes{Val: ""}, 1},

	{"string - prefix - valid", &cases.StringPrefix{Val: "foobar"}, 0},
	{"string - prefix - valid (only)", &cases.StringPrefix{Val: "foo"}, 0},
	{"string - prefix - invalid", &cases.StringPrefix{Val: "bar"}, 1},
	{"string - prefix - invalid (case-sensitive)", &cases.StringPrefix{Val: "Foobar"}, 1},

	{"string - contains - valid", &cases.StringContains{Val: "candy bars"}, 0},
	{"string - contains - valid (only)", &cases.StringContains{Val: "bar"}, 0},
	{"string - contains - invalid", &cases.StringContains{Val: "candy bazs"}, 1},
	{"string - contains - invalid (case-sensitive)", &cases.StringContains{Val: "Candy Bars"}, 1},

	{"string - not contains - valid", &cases.StringNotContains{Val: "candy bazs"}, 0},
	{"string - not contains - valid (case-sensitive)", &cases.StringNotContains{Val: "Candy Bars"}, 0},
	{"string - not contains - invalid", &cases.StringNotContains{Val: "candy bars"}, 1},
	{"string - not contains - invalid (equal)", &cases.StringNotContains{Val: "bar"}, 1},

	{"string - suffix - valid", &cases.StringSuffix{Val: "foobaz"}, 0},
	{"string - suffix - valid (only)", &cases.StringSuffix{Val: "baz"}, 0},
	{"string - suffix - invalid", &cases.StringSuffix{Val: "foobar"}, 1},
	{"string - suffix - invalid (case-sensitive)", &cases.StringSuffix{Val: "FooBaz"}, 1},

	{"string - email - valid", &cases.StringEmail{Val: "foo@bar.com"}, 0},
	{"string - email - valid (name)", &cases.StringEmail{Val: "John Smith <foo@bar.com>"}, 0},
	{"string - email - invalid", &cases.StringEmail{Val: "foobar"}, 1},
	{"string - email - invalid (local segment too long)", &cases.StringEmail{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789@example.com"}, 1},
	{"string - email - invalid (hostname too long)", &cases.StringEmail{Val: "foo@x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, 1},
	{"string - email - invalid (bad hostname)", &cases.StringEmail{Val: "foo@-bar.com"}, 1},
	{"string - email - empty", &cases.StringEmail{Val: ""}, 1},

	{"string - address - valid hostname", &cases.StringAddress{Val: "example.com"}, 0},
	{"string - address - valid hostname (uppercase)", &cases.StringAddress{Val: "ASD.example.com"}, 0},
	{"string - address - valid hostname (hyphens)", &cases.StringAddress{Val: "foo-bar.com"}, 0},
	{"string - address - valid hostname (trailing dot)", &cases.StringAddress{Val: "example.com."}, 0},
	{"string - address - invalid hostname", &cases.StringAddress{Val: "!@#$%^&"}, 1},
	{"string - address - invalid hostname (underscore)", &cases.StringAddress{Val: "foo_bar.com"}, 1},
	{"string - address - invalid hostname (too long)", &cases.StringAddress{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, 1},
	{"string - address - invalid hostname (trailing hyphens)", &cases.StringAddress{Val: "foo-bar-.com"}, 1},
	{"string - address - invalid hostname (leading hyphens)", &cases.StringAddress{Val: "foo-bar.-com"}, 1},
	{"string - address - invalid hostname (empty)", &cases.StringAddress{Val: "asd..asd.com"}, 1},
	{"string - address - invalid hostname (IDNs)", &cases.StringAddress{Val: "你好.com"}, 1},
	{"string - address - valid ip (v4)", &cases.StringAddress{Val: "192.168.0.1"}, 0},
	{"string - address - valid ip (v6)", &cases.StringAddress{Val: "3e::99"}, 0},
	{"string - address - invalid ip", &cases.StringAddress{Val: "ff::fff::0b"}, 1},

	{"string - hostname - valid", &cases.StringHostname{Val: "example.com"}, 0},
	{"string - hostname - valid (uppercase)", &cases.StringHostname{Val: "ASD.example.com"}, 0},
	{"string - hostname - valid (hyphens)", &cases.StringHostname{Val: "foo-bar.com"}, 0},
	{"string - hostname - valid (trailing dot)", &cases.StringHostname{Val: "example.com."}, 0},
	{"string - hostname - invalid", &cases.StringHostname{Val: "!@#$%^&"}, 1},
	{"string - hostname - invalid (underscore)", &cases.StringHostname{Val: "foo_bar.com"}, 1},
	{"string - hostname - invalid (too long)", &cases.StringHostname{Val: "x0123456789012345678901234567890123456789012345678901234567890123456789.com"}, 1},
	{"string - hostname - invalid (trailing hyphens)", &cases.StringHostname{Val: "foo-bar-.com"}, 1},
	{"string - hostname - invalid (leading hyphens)", &cases.StringHostname{Val: "foo-bar.-com"}, 1},
	{"string - hostname - invalid (empty)", &cases.StringHostname{Val: "asd..asd.com"}, 1},
	{"string - hostname - invalid (IDNs)", &cases.StringHostname{Val: "你好.com"}, 1},

	{"string - IP - valid (v4)", &cases.StringIP{Val: "192.168.0.1"}, 0},
	{"string - IP - valid (v6)", &cases.StringIP{Val: "3e::99"}, 0},
	{"string - IP - invalid", &cases.StringIP{Val: "foobar"}, 1},

	{"string - IPv4 - valid", &cases.StringIPv4{Val: "192.168.0.1"}, 0},
	{"string - IPv4 - invalid", &cases.StringIPv4{Val: "foobar"}, 1},
	{"string - IPv4 - invalid (erroneous)", &cases.StringIPv4{Val: "256.0.0.0"}, 1},
	{"string - IPv4 - invalid (v6)", &cases.StringIPv4{Val: "3e::99"}, 1},

	{"string - IPv6 - valid", &cases.StringIPv6{Val: "2001:0db8:85a3:0000:0000:8a2e:0370:7334"}, 0},
	{"string - IPv6 - valid (collapsed)", &cases.StringIPv6{Val: "2001:db8:85a3::8a2e:370:7334"}, 0},
	{"string - IPv6 - invalid", &cases.StringIPv6{Val: "foobar"}, 1},
	{"string - IPv6 - invalid (v4)", &cases.StringIPv6{Val: "192.168.0.1"}, 1},
	{"string - IPv6 - invalid (erroneous)", &cases.StringIPv6{Val: "ff::fff::0b"}, 1},

	{"string - URI - valid", &cases.StringURI{Val: "http://example.com/foo/bar?baz=quux"}, 0},
	{"string - URI - invalid", &cases.StringURI{Val: "!@#$%^&*%$#"}, 1},
	{"string - URI - invalid (relative)", &cases.StringURI{Val: "/foo/bar?baz=quux"}, 1},

	{"string - URI - valid", &cases.StringURIRef{Val: "http://example.com/foo/bar?baz=quux"}, 0},
	{"string - URI - valid (relative)", &cases.StringURIRef{Val: "/foo/bar?baz=quux"}, 0},
	{"string - URI - invalid", &cases.StringURIRef{Val: "!@#$%^&*%$#"}, 1},

	{"string - UUID - valid (nil)", &cases.StringUUID{Val: "00000000-0000-0000-0000-000000000000"}, 0},
	{"string - UUID - valid (v1)", &cases.StringUUID{Val: "b45c0c80-8880-11e9-a5b1-000000000000"}, 0},
	{"string - UUID - valid (v1 - case-insensitive)", &cases.StringUUID{Val: "B45C0C80-8880-11E9-A5B1-000000000000"}, 0},
	{"string - UUID - valid (v2)", &cases.StringUUID{Val: "b45c0c80-8880-21e9-a5b1-000000000000"}, 0},
	{"string - UUID - valid (v2 - case-insensitive)", &cases.StringUUID{Val: "B45C0C80-8880-21E9-A5B1-000000000000"}, 0},
	{"string - UUID - valid (v3)", &cases.StringUUID{Val: "a3bb189e-8bf9-3888-9912-ace4e6543002"}, 0},
	{"string - UUID - valid (v3 - case-insensitive)", &cases.StringUUID{Val: "A3BB189E-8BF9-3888-9912-ACE4E6543002"}, 0},
	{"string - UUID - valid (v4)", &cases.StringUUID{Val: "8b208305-00e8-4460-a440-5e0dcd83bb0a"}, 0},
	{"string - UUID - valid (v4 - case-insensitive)", &cases.StringUUID{Val: "8B208305-00E8-4460-A440-5E0DCD83BB0A"}, 0},
	{"string - UUID - valid (v5)", &cases.StringUUID{Val: "a6edc906-2f9f-5fb2-a373-efac406f0ef2"}, 0},
	{"string - UUID - valid (v5 - case-insensitive)", &cases.StringUUID{Val: "A6EDC906-2F9F-5FB2-A373-EFAC406F0EF2"}, 0},
	{"string - UUID - invalid", &cases.StringUUID{Val: "foobar"}, 1},
	{"string - UUID - invalid (bad UUID)", &cases.StringUUID{Val: "ffffffff-ffff-ffff-ffff-fffffffffffff"}, 1},
	{"string - UUID - valid (ignore_empty)", &cases.StringUUIDIgnore{Val: ""}, 0},

	{"string - http header name - valid", &cases.StringHttpHeaderName{Val: "clustername"}, 0},
	{"string - http header name - valid", &cases.StringHttpHeaderName{Val: ":path"}, 0},
	{"string - http header name - valid (nums)", &cases.StringHttpHeaderName{Val: "cluster-123"}, 0},
	{"string - http header name - valid (special token)", &cases.StringHttpHeaderName{Val: "!+#&.%"}, 0},
	{"string - http header name - valid (period)", &cases.StringHttpHeaderName{Val: "CLUSTER.NAME"}, 0},
	{"string - http header name - invalid", &cases.StringHttpHeaderName{Val: ":"}, 1},
	{"string - http header name - invalid", &cases.StringHttpHeaderName{Val: ":path:"}, 1},
	{"string - http header name - invalid (space)", &cases.StringHttpHeaderName{Val: "cluster name"}, 1},
	{"string - http header name - invalid (return)", &cases.StringHttpHeaderName{Val: "example\r"}, 1},
	{"string - http header name - invalid (tab)", &cases.StringHttpHeaderName{Val: "example\t"}, 1},
	{"string - http header name - invalid (slash)", &cases.StringHttpHeaderName{Val: "/test/long/url"}, 1},

	{"string - http header value - valid", &cases.StringHttpHeaderValue{Val: "cluster.name.123"}, 0},
	{"string - http header value - valid (uppercase)", &cases.StringHttpHeaderValue{Val: "/TEST/LONG/URL"}, 0},
	{"string - http header value - valid (spaces)", &cases.StringHttpHeaderValue{Val: "cluster name"}, 0},
	{"string - http header value - valid (tab)", &cases.StringHttpHeaderValue{Val: "example\t"}, 0},
	{"string - http header value - valid (special token)", &cases.StringHttpHeaderValue{Val: "!#%&./+"}, 0},
	{"string - http header value - invalid (NUL)", &cases.StringHttpHeaderValue{Val: "foo\u0000bar"}, 1},
	{"string - http header value - invalid (DEL)", &cases.StringHttpHeaderValue{Val: "\u007f"}, 1},
	{"string - http header value - invalid", &cases.StringHttpHeaderValue{Val: "example\r"}, 1},

	{"string - non-strict valid header - valid", &cases.StringValidHeader{Val: "cluster.name.123"}, 0},
	{"string - non-strict valid header - valid (uppercase)", &cases.StringValidHeader{Val: "/TEST/LONG/URL"}, 0},
	{"string - non-strict valid header - valid (spaces)", &cases.StringValidHeader{Val: "cluster name"}, 0},
	{"string - non-strict valid header - valid (tab)", &cases.StringValidHeader{Val: "example\t"}, 0},
	{"string - non-strict valid header - valid (DEL)", &cases.StringValidHeader{Val: "\u007f"}, 0},
	{"string - non-strict valid header - invalid (NUL)", &cases.StringValidHeader{Val: "foo\u0000bar"}, 1},
	{"string - non-strict valid header - invalid (CR)", &cases.StringValidHeader{Val: "example\r"}, 1},
	{"string - non-strict valid header - invalid (NL)", &cases.StringValidHeader{Val: "exa\u000Ample"}, 1},
}

var bytesCases = []TestCase{
	{"bytes - none - valid", &cases.BytesNone{Val: []byte("quux")}, 0},

	{"bytes - const - valid", &cases.BytesConst{Val: []byte("foo")}, 0},
	{"bytes - const - invalid", &cases.BytesConst{Val: []byte("bar")}, 1},

	{"bytes - in - valid", &cases.BytesIn{Val: []byte("bar")}, 0},
	{"bytes - in - invalid", &cases.BytesIn{Val: []byte("quux")}, 1},
	{"bytes - not in - valid", &cases.BytesNotIn{Val: []byte("quux")}, 0},
	{"bytes - not in - invalid", &cases.BytesNotIn{Val: []byte("fizz")}, 1},

	{"bytes - len - valid", &cases.BytesLen{Val: []byte("baz")}, 0},
	{"bytes - len - invalid (lt)", &cases.BytesLen{Val: []byte("go")}, 1},
	{"bytes - len - invalid (gt)", &cases.BytesLen{Val: []byte("fizz")}, 1},

	{"bytes - min len - valid", &cases.BytesMinLen{Val: []byte("fizz")}, 0},
	{"bytes - min len - valid (min)", &cases.BytesMinLen{Val: []byte("baz")}, 0},
	{"bytes - min len - invalid", &cases.BytesMinLen{Val: []byte("go")}, 1},

	{"bytes - max len - valid", &cases.BytesMaxLen{Val: []byte("foo")}, 0},
	{"bytes - max len - valid (max)", &cases.BytesMaxLen{Val: []byte("proto")}, 0},
	{"bytes - max len - invalid", &cases.BytesMaxLen{Val: []byte("1234567890")}, 1},

	{"bytes - min/max len - valid", &cases.BytesMinMaxLen{Val: []byte("quux")}, 0},
	{"bytes - min/max len - valid (min)", &cases.BytesMinMaxLen{Val: []byte("foo")}, 0},
	{"bytes - min/max len - valid (max)", &cases.BytesMinMaxLen{Val: []byte("proto")}, 0},
	{"bytes - min/max len - invalid (below)", &cases.BytesMinMaxLen{Val: []byte("go")}, 1},
	{"bytes - min/max len - invalid (above)", &cases.BytesMinMaxLen{Val: []byte("validate")}, 1},

	{"bytes - equal min/max len - valid", &cases.BytesEqualMinMaxLen{Val: []byte("proto")}, 0},
	{"bytes - equal min/max len - invalid", &cases.BytesEqualMinMaxLen{Val: []byte("validate")}, 1},

	{"bytes - pattern - valid", &cases.BytesPattern{Val: []byte("Foo123")}, 0},
	{"bytes - pattern - invalid", &cases.BytesPattern{Val: []byte("你好你好")}, 1},
	{"bytes - pattern - invalid (empty)", &cases.BytesPattern{Val: []byte("")}, 1},

	{"bytes - prefix - valid", &cases.BytesPrefix{Val: []byte{0x99, 0x9f, 0x08}}, 0},
	{"bytes - prefix - valid (only)", &cases.BytesPrefix{Val: []byte{0x99}}, 0},
	{"bytes - prefix - invalid", &cases.BytesPrefix{Val: []byte("bar")}, 1},

	{"bytes - contains - valid", &cases.BytesContains{Val: []byte("candy bars")}, 0},
	{"bytes - contains - valid (only)", &cases.BytesContains{Val: []byte("bar")}, 0},
	{"bytes - contains - invalid", &cases.BytesContains{Val: []byte("candy bazs")}, 1},

	{"bytes - suffix - valid", &cases.BytesSuffix{Val: []byte{0x62, 0x75, 0x7A, 0x7A}}, 0},
	{"bytes - suffix - valid (only)", &cases.BytesSuffix{Val: []byte("\x62\x75\x7A\x7A")}, 0},
	{"bytes - suffix - invalid", &cases.BytesSuffix{Val: []byte("foobar")}, 1},
	{"bytes - suffix - invalid (case-sensitive)", &cases.BytesSuffix{Val: []byte("FooBaz")}, 1},

	{"bytes - IP - valid (v4)", &cases.BytesIP{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, 0},
	{"bytes - IP - valid (v6)", &cases.BytesIP{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, 0},
	{"bytes - IP - invalid", &cases.BytesIP{Val: []byte("foobar")}, 1},

	{"bytes - IPv4 - valid", &cases.BytesIPv4{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, 0},
	{"bytes - IPv4 - invalid", &cases.BytesIPv4{Val: []byte("foobar")}, 1},
	{"bytes - IPv4 - invalid (v6)", &cases.BytesIPv4{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, 1},

	{"bytes - IPv6 - valid", &cases.BytesIPv6{Val: []byte("\x20\x01\x0D\xB8\x85\xA3\x00\x00\x00\x00\x8A\x2E\x03\x70\x73\x34")}, 0},
	{"bytes - IPv6 - invalid", &cases.BytesIPv6{Val: []byte("fooar")}, 1},
	{"bytes - IPv6 - invalid (v4)", &cases.BytesIPv6{Val: []byte{0xC0, 0xA8, 0x00, 0x01}}, 1},

	{"bytes - IPv6 - valid (ignore_empty)", &cases.BytesIPv6Ignore{Val: nil}, 0},
}

var enumCases = []TestCase{
	{"enum - none - valid", &cases.EnumNone{Val: cases.TestEnum_ONE}, 0},

	{"enum - const - valid", &cases.EnumConst{Val: cases.TestEnum_TWO}, 0},
	{"enum - const - invalid", &cases.EnumConst{Val: cases.TestEnum_ONE}, 1},
	{"enum alias - const - valid", &cases.EnumAliasConst{Val: cases.TestEnumAlias_C}, 0},
	{"enum alias - const - valid (alias)", &cases.EnumAliasConst{Val: cases.TestEnumAlias_GAMMA}, 0},
	{"enum alias - const - invalid", &cases.EnumAliasConst{Val: cases.TestEnumAlias_ALPHA}, 1},

	{"enum - defined_only - valid", &cases.EnumDefined{Val: 0}, 0},
	{"enum - defined_only - invalid", &cases.EnumDefined{Val: math.MaxInt32}, 1},
	{"enum alias - defined_only - valid", &cases.EnumAliasDefined{Val: 1}, 0},
	{"enum alias - defined_only - invalid", &cases.EnumAliasDefined{Val: math.MaxInt32}, 1},

	{"enum - in - valid", &cases.EnumIn{Val: cases.TestEnum_TWO}, 0},
	{"enum - in - invalid", &cases.EnumIn{Val: cases.TestEnum_ONE}, 1},
	{"enum alias - in - valid", &cases.EnumAliasIn{Val: cases.TestEnumAlias_A}, 0},
	{"enum alias - in - valid (alias)", &cases.EnumAliasIn{Val: cases.TestEnumAlias_ALPHA}, 0},
	{"enum alias - in - invalid", &cases.EnumAliasIn{Val: cases.TestEnumAlias_BETA}, 1},

	{"enum - not in - valid", &cases.EnumNotIn{Val: cases.TestEnum_ZERO}, 0},
	{"enum - not in - valid (undefined)", &cases.EnumNotIn{Val: math.MaxInt32}, 0},
	{"enum - not in - invalid", &cases.EnumNotIn{Val: cases.TestEnum_ONE}, 1},
	{"enum alias - not in - valid", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_ALPHA}, 0},
	{"enum alias - not in - invalid", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_B}, 1},
	{"enum alias - not in - invalid (alias)", &cases.EnumAliasNotIn{Val: cases.TestEnumAlias_BETA}, 1},

	{"enum external - defined_only - valid", &cases.EnumExternal{Val: other_package.Embed_VALUE}, 0},
	{"enum external - defined_only - invalid", &cases.EnumExternal{Val: math.MaxInt32}, 1},
	{"enum external - in - valid", &cases.EnumExternal3{Foo: other_package.Embed_ZERO}, 0},
	{"enum external - in - invalid", &cases.EnumExternal3{Foo: other_package.Embed_ONE}, 1},
	{"enum external - not in - valid", &cases.EnumExternal3{Bar: yet_another_package.Embed_ZERO}, 0},
	{"enum external - not in - invalid", &cases.EnumExternal3{Bar: yet_another_package.Embed_ONE}, 1},
	{"enum external - const - valid", &cases.EnumExternal4{SortDirection: sort.Direction_ASC}, 0},
	{"enum external - const - invalid", &cases.EnumExternal4{SortDirection: sort.Direction_DESC}, 1},

	{"enum repeated - defined_only - valid", &cases.RepeatedEnumDefined{Val: []cases.TestEnum{cases.TestEnum_ONE, cases.TestEnum_TWO}}, 0},
	{"enum repeated - defined_only - invalid", &cases.RepeatedEnumDefined{Val: []cases.TestEnum{cases.TestEnum_ONE, math.MaxInt32}}, 1},

	{"enum repeated (external) - defined_only - valid", &cases.RepeatedExternalEnumDefined{Val: []other_package.Embed_Enumerated{other_package.Embed_VALUE}}, 0},
	{"enum repeated (external) - defined_only - invalid", &cases.RepeatedExternalEnumDefined{Val: []other_package.Embed_Enumerated{math.MaxInt32}}, 1},

	{"enum repeated (another external) - defined_only - valid", &cases.RepeatedYetAnotherExternalEnumDefined{Val: []yet_another_package.Embed_Enumerated{yet_another_package.Embed_VALUE}}, 0},

	{"enum repeated (external) - in - valid", &cases.RepeatedEnumExternal{Foo: []other_package.Embed_FooNumber{other_package.Embed_ZERO, other_package.Embed_TWO}}, 0},
	{"enum repeated (external) - in - invalid", &cases.RepeatedEnumExternal{Foo: []other_package.Embed_FooNumber{other_package.Embed_ONE}}, 1},
	{"enum repeated (external) - not in - valid", &cases.RepeatedEnumExternal{Bar: []yet_another_package.Embed_BarNumber{yet_another_package.Embed_ZERO, yet_another_package.Embed_TWO}}, 0},
	{"enum repeated (external) - not in - invalid", &cases.RepeatedEnumExternal{Bar: []yet_another_package.Embed_BarNumber{yet_another_package.Embed_ONE}}, 1},

	{"enum map - defined_only - valid", &cases.MapEnumDefined{Val: map[string]cases.TestEnum{"foo": cases.TestEnum_TWO}}, 0},
	{"enum map - defined_only - invalid", &cases.MapEnumDefined{Val: map[string]cases.TestEnum{"foo": math.MaxInt32}}, 1},

	{"enum map (external) - defined_only - valid", &cases.MapExternalEnumDefined{Val: map[string]other_package.Embed_Enumerated{"foo": other_package.Embed_VALUE}}, 0},
	{"enum map (external) - defined_only - invalid", &cases.MapExternalEnumDefined{Val: map[string]other_package.Embed_Enumerated{"foo": math.MaxInt32}}, 1},
}

var messageCases = []TestCase{
	{"message - none - valid", &cases.MessageNone{Val: &cases.MessageNone_NoneMsg{}}, 0},
	{"message - none - valid (unset)", &cases.MessageNone{}, 0},

	{"message - disabled - valid", &cases.MessageDisabled{Val: 456}, 0},
	{"message - disabled - valid (invalid field)", &cases.MessageDisabled{Val: 0}, 0},

	{"message - ignored - valid", &cases.MessageIgnored{Val: 456}, 0},
	{"message - ignored - valid (invalid field)", &cases.MessageIgnored{Val: 0}, 0},

	{"message - field - valid", &cases.Message{Val: &cases.TestMsg{Const: "foo"}}, 0},
	{"message - field - valid (unset)", &cases.Message{}, 0},
	{"message - field - invalid", &cases.Message{Val: &cases.TestMsg{}}, 1},
	{"message - field - invalid (transitive)", &cases.Message{Val: &cases.TestMsg{Const: "foo", Nested: &cases.TestMsg{}}}, 1},

	{"message - skip - valid", &cases.MessageSkip{Val: &cases.TestMsg{}}, 0},

	{"message - required - valid", &cases.MessageRequired{Val: &cases.TestMsg{Const: "foo"}}, 0},
	{"message - required - valid (oneof)", &cases.MessageRequiredOneof{One: &cases.MessageRequiredOneof_Val{Val: &cases.TestMsg{Const: "foo"}}}, 0},
	{"message - required - invalid", &cases.MessageRequired{}, 1},
	{"message - required - invalid (oneof)", &cases.MessageRequiredOneof{}, 1},

	{"message - cross-package embed none - valid", &cases.MessageCrossPackage{Val: &other_package.Embed{Val: 1}}, 0},
	{"message - cross-package embed none - valid (nil)", &cases.MessageCrossPackage{}, 0},
	{"message - cross-package embed none - valid (empty)", &cases.MessageCrossPackage{Val: &other_package.Embed{}}, 1},
	{"message - cross-package embed none - invalid", &cases.MessageCrossPackage{Val: &other_package.Embed{Val: -1}}, 1},

	{"message - required - valid", &cases.MessageRequiredButOptional{Val: &cases.TestMsg{Const: "foo"}}, 0},
	{"message - required - valid (unset)", &cases.MessageRequiredButOptional{}, 0},
}

var repeatedCases = []TestCase{
	{"repeated - none - valid", &cases.RepeatedNone{Val: []int64{1, 2, 3}}, 0},

	{"repeated - embed none - valid", &cases.RepeatedEmbedNone{Val: []*cases.Embed{{Val: 1}}}, 0},
	{"repeated - embed none - valid (nil)", &cases.RepeatedEmbedNone{}, 0},
	{"repeated - embed none - valid (empty)", &cases.RepeatedEmbedNone{Val: []*cases.Embed{}}, 0},
	{"repeated - embed none - invalid", &cases.RepeatedEmbedNone{Val: []*cases.Embed{{Val: -1}}}, 1},

	{"repeated - cross-package embed none - valid", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{{Val: 1}}}, 0},
	{"repeated - cross-package embed none - valid (nil)", &cases.RepeatedEmbedCrossPackageNone{}, 0},
	{"repeated - cross-package embed none - valid (empty)", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{}}, 0},
	{"repeated - cross-package embed none - invalid", &cases.RepeatedEmbedCrossPackageNone{Val: []*other_package.Embed{{Val: -1}}}, 1},

	{"repeated - min - valid", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: 2}, {Val: 3}}}, 0},
	{"repeated - min - valid (equal)", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: 2}}}, 0},
	{"repeated - min - invalid", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}}}, 1},
	{"repeated - min - invalid (element)", &cases.RepeatedMin{Val: []*cases.Embed{{Val: 1}, {Val: -1}}}, 1},

	{"repeated - max - valid", &cases.RepeatedMax{Val: []float64{1, 2}}, 0},
	{"repeated - max - valid (equal)", &cases.RepeatedMax{Val: []float64{1, 2, 3}}, 0},
	{"repeated - max - invalid", &cases.RepeatedMax{Val: []float64{1, 2, 3, 4}}, 1},

	{"repeated - min/max - valid", &cases.RepeatedMinMax{Val: []int32{1, 2, 3}}, 0},
	{"repeated - min/max - valid (min)", &cases.RepeatedMinMax{Val: []int32{1, 2}}, 0},
	{"repeated - min/max - valid (max)", &cases.RepeatedMinMax{Val: []int32{1, 2, 3, 4}}, 0},
	{"repeated - min/max - invalid (below)", &cases.RepeatedMinMax{Val: []int32{}}, 1},
	{"repeated - min/max - invalid (above)", &cases.RepeatedMinMax{Val: []int32{1, 2, 3, 4, 5}}, 1},

	{"repeated - exact - valid", &cases.RepeatedExact{Val: []uint32{1, 2, 3}}, 0},
	{"repeated - exact - invalid (below)", &cases.RepeatedExact{Val: []uint32{1, 2}}, 1},
	{"repeated - exact - invalid (above)", &cases.RepeatedExact{Val: []uint32{1, 2, 3, 4}}, 1},

	{"repeated - unique - valid", &cases.RepeatedUnique{Val: []string{"foo", "bar", "baz"}}, 0},
	{"repeated - unique - valid (empty)", &cases.RepeatedUnique{}, 0},
	{"repeated - unique - valid (case sensitivity)", &cases.RepeatedUnique{Val: []string{"foo", "Foo"}}, 0},
	{"repeated - unique - invalid", &cases.RepeatedUnique{Val: []string{"foo", "bar", "foo", "baz"}}, 1},

	{"repeated - items - valid", &cases.RepeatedItemRule{Val: []float32{1, 2, 3}}, 0},
	{"repeated - items - valid (empty)", &cases.RepeatedItemRule{Val: []float32{}}, 0},
	{"repeated - items - valid (pattern)", &cases.RepeatedItemPattern{Val: []string{"Alpha", "Beta123"}}, 0},
	{"repeated - items - invalid", &cases.RepeatedItemRule{Val: []float32{1, -2, 3}}, 1},
	{"repeated - items - invalid (pattern)", &cases.RepeatedItemPattern{Val: []string{"Alpha", "!@#$%^&*()"}}, 1},
	{"repeated - items - invalid (in)", &cases.RepeatedItemIn{Val: []string{"baz"}}, 1},
	{"repeated - items - valid (in)", &cases.RepeatedItemIn{Val: []string{"foo"}}, 0},
	{"repeated - items - invalid (not_in)", &cases.RepeatedItemNotIn{Val: []string{"foo"}}, 1},
	{"repeated - items - valid (not_in)", &cases.RepeatedItemNotIn{Val: []string{"baz"}}, 0},
	{"repeated - items - invalid (int64 in)", &cases.RepeatedItemInt64In{Val: []int64{3}}, 1},
	{"repeated - items - valid (int64 in)", &cases.RepeatedItemInt64In{Val: []int64{1}}, 0},
	{"repeated - items - invalid (int64 not_in)", &cases.RepeatedItemInt64NotIn{Val: []int64{1}}, 1},
	{"repeated - items - valid (int64 not_in)", &cases.RepeatedItemInt64NotIn{Val: []int64{3}}, 0},
	{"repeated - items - invalid (int32 in)", &cases.RepeatedItemInt32In{Val: []int32{3}}, 1},
	{"repeated - items - valid (int32 in)", &cases.RepeatedItemInt32In{Val: []int32{1}}, 0},
	{"repeated - items - invalid (int32 not_in)", &cases.RepeatedItemInt32NotIn{Val: []int32{1}}, 1},
	{"repeated - items - valid (int32 not_in)", &cases.RepeatedItemInt32NotIn{Val: []int32{3}}, 0},
	{"repeated - items - invalid (enum in)", &cases.RepeatedEnumIn{Val: []cases.AnEnum{1}}, 1},
	{"repeated - items - valid (enum in)", &cases.RepeatedEnumIn{Val: []cases.AnEnum{0}}, 0},
	{"repeated - items - invalid (enum not_in)", &cases.RepeatedEnumNotIn{Val: []cases.AnEnum{0}}, 1},
	{"repeated - items - valid (enum not_in)", &cases.RepeatedEnumNotIn{Val: []cases.AnEnum{1}}, 0},
	{"repeated - items - invalid (embedded enum in)", &cases.RepeatedEmbeddedEnumIn{Val: []cases.RepeatedEmbeddedEnumIn_AnotherInEnum{1}}, 1},
	{"repeated - items - valid (embedded enum in)", &cases.RepeatedEmbeddedEnumIn{Val: []cases.RepeatedEmbeddedEnumIn_AnotherInEnum{0}}, 0},
	{"repeated - items - invalid (embedded enum not_in)", &cases.RepeatedEmbeddedEnumNotIn{Val: []cases.RepeatedEmbeddedEnumNotIn_AnotherNotInEnum{0}}, 1},
	{"repeated - items - valid (embedded enum not_in)", &cases.RepeatedEmbeddedEnumNotIn{Val: []cases.RepeatedEmbeddedEnumNotIn_AnotherNotInEnum{1}}, 0},

	{"repeated - items - invalid (any in)", &cases.RepeatedAnyIn{Val: []*anypb.Any{{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}}, 1},
	{"repeated - items - valid (any in)", &cases.RepeatedAnyIn{Val: []*anypb.Any{{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}}, 0},
	{"repeated - items - invalid (any not_in)", &cases.RepeatedAnyNotIn{Val: []*anypb.Any{{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}}, 1},
	{"repeated - items - valid (any not_in)", &cases.RepeatedAnyNotIn{Val: []*anypb.Any{{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}}, 0},

	{"repeated - embed skip - valid", &cases.RepeatedEmbedSkip{Val: []*cases.Embed{{Val: 1}}}, 0},
	{"repeated - embed skip - valid (invalid element)", &cases.RepeatedEmbedSkip{Val: []*cases.Embed{{Val: -1}}}, 0},
	{"repeated - min and items len - valid", &cases.RepeatedMinAndItemLen{Val: []string{"aaa", "bbb"}}, 0},
	{"repeated - min and items len - invalid (min)", &cases.RepeatedMinAndItemLen{Val: []string{}}, 1},
	{"repeated - min and items len - invalid (len)", &cases.RepeatedMinAndItemLen{Val: []string{"x"}}, 1},
	{"repeated - min and max items len - valid", &cases.RepeatedMinAndMaxItemLen{Val: []string{"aaa", "bbb"}}, 0},
	{"repeated - min and max items len - invalid (min_len)", &cases.RepeatedMinAndMaxItemLen{}, 1},
	{"repeated - min and max items len - invalid (max_len)", &cases.RepeatedMinAndMaxItemLen{Val: []string{"aaa", "bbb", "ccc", "ddd"}}, 1},

	{"repeated - duration - gte - valid", &cases.RepeatedDuration{Val: []*durationpb.Duration{{Seconds: 3}}}, 0},
	{"repeated - duration - gte - valid (empty)", &cases.RepeatedDuration{}, 0},
	{"repeated - duration - gte - valid (equal)", &cases.RepeatedDuration{Val: []*durationpb.Duration{{Nanos: 1000000}}}, 0},
	{"repeated - duration - gte - invalid", &cases.RepeatedDuration{Val: []*durationpb.Duration{{Seconds: -1}}}, 1},

	{"repeated - exact - valid (ignore_empty)", &cases.RepeatedExactIgnore{Val: nil}, 0},
}

var mapCases = []TestCase{
	{"map - none - valid", &cases.MapNone{Val: map[uint32]bool{123: true, 456: false}}, 0},

	{"map - min pairs - valid", &cases.MapMin{Val: map[int32]float32{1: 2, 3: 4, 5: 6}}, 0},
	{"map - min pairs - valid (equal)", &cases.MapMin{Val: map[int32]float32{1: 2, 3: 4}}, 0},
	{"map - min pairs - invalid", &cases.MapMin{Val: map[int32]float32{1: 2}}, 1},

	{"map - max pairs - valid", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4}}, 0},
	{"map - max pairs - valid (equal)", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4, 5: 6}}, 0},
	{"map - max pairs - invalid", &cases.MapMax{Val: map[int64]float64{1: 2, 3: 4, 5: 6, 7: 8}}, 1},

	{"map - min/max - valid", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true}}, 0},
	{"map - min/max - valid (min)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false}}, 0},
	{"map - min/max - valid (max)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true, "d": false}}, 0},
	{"map - min/max - invalid (below)", &cases.MapMinMax{Val: map[string]bool{}}, 1},
	{"map - min/max - invalid (above)", &cases.MapMinMax{Val: map[string]bool{"a": true, "b": false, "c": true, "d": false, "e": true}}, 1},

	{"map - exact - valid", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b", 3: "c"}}, 0},
	{"map - exact - invalid (below)", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b"}}, 1},
	{"map - exact - invalid (above)", &cases.MapExact{Val: map[uint64]string{1: "a", 2: "b", 3: "c", 4: "d"}}, 1},

	{"map - no sparse - valid", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{1: {}, 2: {}}}, 0},
	{"map - no sparse - valid (empty)", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{}}, 0},
	// sparse maps are no longer supported, so this case is no longer possible
	//{"map - no sparse - invalid", &cases.MapNoSparse{Val: map[uint32]*cases.MapNoSparse_Msg{1: {}, 2: nil}}, 1},

	{"map - keys - valid", &cases.MapKeys{Val: map[int64]string{-1: "a", -2: "b"}}, 0},
	{"map - keys - valid (empty)", &cases.MapKeys{Val: map[int64]string{}}, 0},
	{"map - keys - valid (pattern)", &cases.MapKeysPattern{Val: map[string]string{"A": "a"}}, 0},
	{"map - keys - valid (in)", &cases.MapKeysIn{Val: map[string]string{"foo": "value"}}, 0},
	{"map - keys - valid (not_in)", &cases.MapKeysNotIn{Val: map[string]string{"baz": "value"}}, 0},
	{"map - keys - invalid", &cases.MapKeys{Val: map[int64]string{1: "a"}}, 1},
	{"map - keys - invalid (pattern)", &cases.MapKeysPattern{Val: map[string]string{"A": "a", "!@#$%^&*()": "b"}}, 1},
	{"map - keys - invalid (in)", &cases.MapKeysIn{Val: map[string]string{"baz": "value"}}, 1},
	{"map - keys - invalid (not_in)", &cases.MapKeysNotIn{Val: map[string]string{"foo": "value"}}, 1},

	{"map - values - valid", &cases.MapValues{Val: map[string]string{"a": "Alpha", "b": "Beta"}}, 0},
	{"map - values - valid (empty)", &cases.MapValues{Val: map[string]string{}}, 0},
	{"map - values - valid (pattern)", &cases.MapValuesPattern{Val: map[string]string{"a": "A"}}, 0},
	{"map - values - invalid", &cases.MapValues{Val: map[string]string{"a": "A", "b": "B"}}, 2},
	{"map - values - invalid (pattern)", &cases.MapValuesPattern{Val: map[string]string{"a": "A", "b": "!@#$%^&*()"}}, 1},

	{"map - recursive - valid", &cases.MapRecursive{Val: map[uint32]*cases.MapRecursive_Msg{1: {Val: "abc"}}}, 0},
	{"map - recursive - invalid", &cases.MapRecursive{Val: map[uint32]*cases.MapRecursive_Msg{1: {}}}, 1},
	{"map - exact - valid (ignore_empty)", &cases.MapExactIgnore{Val: nil}, 0},
	{"map - multiple - valid", &cases.MultipleMaps{First: map[uint32]string{1: "a", 2: "b"}, Second: map[int32]bool{-1: true, -2: false}}, 0},
}

var oneofCases = []TestCase{
	{"oneof - none - valid", &cases.OneOfNone{O: &cases.OneOfNone_X{X: "foo"}}, 0},
	{"oneof - none - valid (empty)", &cases.OneOfNone{}, 0},

	{"oneof - field - valid (X)", &cases.OneOf{O: &cases.OneOf_X{X: "foobar"}}, 0},
	{"oneof - field - valid (Y)", &cases.OneOf{O: &cases.OneOf_Y{Y: 123}}, 0},
	{"oneof - field - valid (Z)", &cases.OneOf{O: &cases.OneOf_Z{Z: &cases.TestOneOfMsg{Val: true}}}, 0},
	{"oneof - field - valid (empty)", &cases.OneOf{}, 0},
	{"oneof - field - invalid (X)", &cases.OneOf{O: &cases.OneOf_X{X: "fizzbuzz"}}, 1},
	{"oneof - field - invalid (Y)", &cases.OneOf{O: &cases.OneOf_Y{Y: -1}}, 1},
	{"oneof - filed - invalid (Z)", &cases.OneOf{O: &cases.OneOf_Z{Z: &cases.TestOneOfMsg{}}}, 1},

	{"oneof - required - valid", &cases.OneOfRequired{O: &cases.OneOfRequired_X{X: ""}}, 0},
	{"oneof - require - invalid", &cases.OneOfRequired{}, 1},

	{"oneof - ignore_empty - valid (X)", &cases.OneOfIgnoreEmpty{O: &cases.OneOfIgnoreEmpty_X{X: ""}}, 0},
	{"oneof - ignore_empty - valid (Y)", &cases.OneOfIgnoreEmpty{O: &cases.OneOfIgnoreEmpty_Y{Y: []byte("")}}, 0},
	{"oneof - ignore_empty - valid (Z)", &cases.OneOfIgnoreEmpty{O: &cases.OneOfIgnoreEmpty_Z{Z: 0}}, 0},
}

var wrapperCases = []TestCase{
	{"wrapper - none - valid", &cases.WrapperNone{Val: &wrapperspb.Int32Value{Value: 123}}, 0},
	{"wrapper - none - valid (empty)", &cases.WrapperNone{Val: nil}, 0},

	{"wrapper - float - valid", &cases.WrapperFloat{Val: &wrapperspb.FloatValue{Value: 1}}, 0},
	{"wrapper - float - valid (empty)", &cases.WrapperFloat{Val: nil}, 0},
	{"wrapper - float - invalid", &cases.WrapperFloat{Val: &wrapperspb.FloatValue{Value: 0}}, 1},

	{"wrapper - double - valid", &cases.WrapperDouble{Val: &wrapperspb.DoubleValue{Value: 1}}, 0},
	{"wrapper - double - valid (empty)", &cases.WrapperDouble{Val: nil}, 0},
	{"wrapper - double - invalid", &cases.WrapperDouble{Val: &wrapperspb.DoubleValue{Value: 0}}, 1},

	{"wrapper - int64 - valid", &cases.WrapperInt64{Val: &wrapperspb.Int64Value{Value: 1}}, 0},
	{"wrapper - int64 - valid (empty)", &cases.WrapperInt64{Val: nil}, 0},
	{"wrapper - int64 - invalid", &cases.WrapperInt64{Val: &wrapperspb.Int64Value{Value: 0}}, 1},

	{"wrapper - int32 - valid", &cases.WrapperInt32{Val: &wrapperspb.Int32Value{Value: 1}}, 0},
	{"wrapper - int32 - valid (empty)", &cases.WrapperInt32{Val: nil}, 0},
	{"wrapper - int32 - invalid", &cases.WrapperInt32{Val: &wrapperspb.Int32Value{Value: 0}}, 1},

	{"wrapper - uint64 - valid", &cases.WrapperUInt64{Val: &wrapperspb.UInt64Value{Value: 1}}, 0},
	{"wrapper - uint64 - valid (empty)", &cases.WrapperUInt64{Val: nil}, 0},
	{"wrapper - uint64 - invalid", &cases.WrapperUInt64{Val: &wrapperspb.UInt64Value{Value: 0}}, 1},

	{"wrapper - uint32 - valid", &cases.WrapperUInt32{Val: &wrapperspb.UInt32Value{Value: 1}}, 0},
	{"wrapper - uint32 - valid (empty)", &cases.WrapperUInt32{Val: nil}, 0},
	{"wrapper - uint32 - invalid", &cases.WrapperUInt32{Val: &wrapperspb.UInt32Value{Value: 0}}, 1},

	{"wrapper - bool - valid", &cases.WrapperBool{Val: &wrapperspb.BoolValue{Value: true}}, 0},
	{"wrapper - bool - valid (empty)", &cases.WrapperBool{Val: nil}, 0},
	{"wrapper - bool - invalid", &cases.WrapperBool{Val: &wrapperspb.BoolValue{Value: false}}, 1},

	{"wrapper - string - valid", &cases.WrapperString{Val: &wrapperspb.StringValue{Value: "foobar"}}, 0},
	{"wrapper - string - valid (empty)", &cases.WrapperString{Val: nil}, 0},
	{"wrapper - string - invalid", &cases.WrapperString{Val: &wrapperspb.StringValue{Value: "fizzbuzz"}}, 1},

	{"wrapper - bytes - valid", &cases.WrapperBytes{Val: &wrapperspb.BytesValue{Value: []byte("foo")}}, 0},
	{"wrapper - bytes - valid (empty)", &cases.WrapperBytes{Val: nil}, 0},
	{"wrapper - bytes - invalid", &cases.WrapperBytes{Val: &wrapperspb.BytesValue{Value: []byte("x")}}, 1},

	{"wrapper - required - string - valid", &cases.WrapperRequiredString{Val: &wrapperspb.StringValue{Value: "bar"}}, 0},
	{"wrapper - required - string - invalid", &cases.WrapperRequiredString{Val: &wrapperspb.StringValue{Value: "foo"}}, 1},
	{"wrapper - required - string - invalid (empty)", &cases.WrapperRequiredString{}, 1},

	{"wrapper - required - string (empty) - valid", &cases.WrapperRequiredEmptyString{Val: &wrapperspb.StringValue{Value: ""}}, 0},
	{"wrapper - required - string (empty) - invalid", &cases.WrapperRequiredEmptyString{Val: &wrapperspb.StringValue{Value: "foo"}}, 1},
	{"wrapper - required - string (empty) - invalid (empty)", &cases.WrapperRequiredEmptyString{}, 1},

	{"wrapper - optional - string (uuid) - valid", &cases.WrapperOptionalUuidString{Val: &wrapperspb.StringValue{Value: "8b72987b-024a-43b3-b4cf-647a1f925c5d"}}, 0},
	{"wrapper - optional - string (uuid) - valid (empty)", &cases.WrapperOptionalUuidString{}, 0},
	{"wrapper - optional - string (uuid) - invalid", &cases.WrapperOptionalUuidString{Val: &wrapperspb.StringValue{Value: "foo"}}, 1},

	{"wrapper - required - float - valid", &cases.WrapperRequiredFloat{Val: &wrapperspb.FloatValue{Value: 1}}, 0},
	{"wrapper - required - float - invalid", &cases.WrapperRequiredFloat{Val: &wrapperspb.FloatValue{Value: -5}}, 1},
	{"wrapper - required - float - invalid (empty)", &cases.WrapperRequiredFloat{}, 1},
}

var durationCases = []TestCase{
	{"duration - none - valid", &cases.DurationNone{Val: &durationpb.Duration{Seconds: 123}}, 0},

	{"duration - required - valid", &cases.DurationRequired{Val: &durationpb.Duration{}}, 0},
	{"duration - required - invalid", &cases.DurationRequired{Val: nil}, 1},

	{"duration - const - valid", &cases.DurationConst{Val: &durationpb.Duration{Seconds: 3}}, 0},
	{"duration - const - valid (empty)", &cases.DurationConst{}, 0},
	{"duration - const - invalid", &cases.DurationConst{Val: &durationpb.Duration{Nanos: 3}}, 1},

	{"duration - in - valid", &cases.DurationIn{Val: &durationpb.Duration{Seconds: 1}}, 0},
	{"duration - in - valid (empty)", &cases.DurationIn{}, 0},
	{"duration - in - invalid", &cases.DurationIn{Val: &durationpb.Duration{}}, 1},

	{"duration - not in - valid", &cases.DurationNotIn{Val: &durationpb.Duration{Nanos: 1}}, 0},
	{"duration - not in - valid (empty)", &cases.DurationNotIn{}, 0},
	{"duration - not in - invalid", &cases.DurationNotIn{Val: &durationpb.Duration{}}, 1},

	{"duration - lt - valid", &cases.DurationLT{Val: &durationpb.Duration{Nanos: -1}}, 0},
	{"duration - lt - valid (empty)", &cases.DurationLT{}, 0},
	{"duration - lt - invalid (equal)", &cases.DurationLT{Val: &durationpb.Duration{}}, 1},
	{"duration - lt - invalid", &cases.DurationLT{Val: &durationpb.Duration{Seconds: 1}}, 1},

	{"duration - lte - valid", &cases.DurationLTE{Val: &durationpb.Duration{}}, 0},
	{"duration - lte - valid (empty)", &cases.DurationLTE{}, 0},
	{"duration - lte - valid (equal)", &cases.DurationLTE{Val: &durationpb.Duration{Seconds: 1}}, 0},
	{"duration - lte - invalid", &cases.DurationLTE{Val: &durationpb.Duration{Seconds: 1, Nanos: 1}}, 1},

	{"duration - gt - valid", &cases.DurationGT{Val: &durationpb.Duration{Seconds: 1}}, 0},
	{"duration - gt - valid (empty)", &cases.DurationGT{}, 0},
	{"duration - gt - invalid (equal)", &cases.DurationGT{Val: &durationpb.Duration{Nanos: 1000}}, 1},
	{"duration - gt - invalid", &cases.DurationGT{Val: &durationpb.Duration{}}, 1},

	{"duration - gte - valid", &cases.DurationGTE{Val: &durationpb.Duration{Seconds: 3}}, 0},
	{"duration - gte - valid (empty)", &cases.DurationGTE{}, 0},
	{"duration - gte - valid (equal)", &cases.DurationGTE{Val: &durationpb.Duration{Nanos: 1000000}}, 0},
	{"duration - gte - invalid", &cases.DurationGTE{Val: &durationpb.Duration{Seconds: -1}}, 1},

	{"duration - gt & lt - valid", &cases.DurationGTLT{Val: &durationpb.Duration{Nanos: 1000}}, 0},
	{"duration - gt & lt - valid (empty)", &cases.DurationGTLT{}, 0},
	{"duration - gt & lt - invalid (above)", &cases.DurationGTLT{Val: &durationpb.Duration{Seconds: 1000}}, 1},
	{"duration - gt & lt - invalid (below)", &cases.DurationGTLT{Val: &durationpb.Duration{Nanos: -1000}}, 1},
	{"duration - gt & lt - invalid (max)", &cases.DurationGTLT{Val: &durationpb.Duration{Seconds: 1}}, 1},
	{"duration - gt & lt - invalid (min)", &cases.DurationGTLT{Val: &durationpb.Duration{}}, 1},

	{"duration - exclusive gt & lt - valid (empty)", &cases.DurationExLTGT{}, 0},
	{"duration - exclusive gt & lt - valid (above)", &cases.DurationExLTGT{Val: &durationpb.Duration{Seconds: 2}}, 0},
	{"duration - exclusive gt & lt - valid (below)", &cases.DurationExLTGT{Val: &durationpb.Duration{Nanos: -1}}, 0},
	{"duration - exclusive gt & lt - invalid", &cases.DurationExLTGT{Val: &durationpb.Duration{Nanos: 1000}}, 1},
	{"duration - exclusive gt & lt - invalid (max)", &cases.DurationExLTGT{Val: &durationpb.Duration{Seconds: 1}}, 1},
	{"duration - exclusive gt & lt - invalid (min)", &cases.DurationExLTGT{Val: &durationpb.Duration{}}, 1},

	{"duration - gte & lte - valid", &cases.DurationGTELTE{Val: &durationpb.Duration{Seconds: 60, Nanos: 1}}, 0},
	{"duration - gte & lte - valid (empty)", &cases.DurationGTELTE{}, 0},
	{"duration - gte & lte - valid (max)", &cases.DurationGTELTE{Val: &durationpb.Duration{Seconds: 3600}}, 0},
	{"duration - gte & lte - valid (min)", &cases.DurationGTELTE{Val: &durationpb.Duration{Seconds: 60}}, 0},
	{"duration - gte & lte - invalid (above)", &cases.DurationGTELTE{Val: &durationpb.Duration{Seconds: 3600, Nanos: 1}}, 1},
	{"duration - gte & lte - invalid (below)", &cases.DurationGTELTE{Val: &durationpb.Duration{Seconds: 59}}, 1},

	{"duration - gte & lte - valid (empty)", &cases.DurationExGTELTE{}, 0},
	{"duration - exclusive gte & lte - valid (above)", &cases.DurationExGTELTE{Val: &durationpb.Duration{Seconds: 3601}}, 0},
	{"duration - exclusive gte & lte - valid (below)", &cases.DurationExGTELTE{Val: &durationpb.Duration{}}, 0},
	{"duration - exclusive gte & lte - valid (max)", &cases.DurationExGTELTE{Val: &durationpb.Duration{Seconds: 3600}}, 0},
	{"duration - exclusive gte & lte - valid (min)", &cases.DurationExGTELTE{Val: &durationpb.Duration{Seconds: 60}}, 0},
	{"duration - exclusive gte & lte - invalid", &cases.DurationExGTELTE{Val: &durationpb.Duration{Seconds: 61}}, 1},
	{"duration - fields with other fields - invalid other field", &cases.DurationFieldWithOtherFields{DurationVal: nil, IntVal: 12}, 1},
}

var timestampCases = []TestCase{
	{"timestamp - none - valid", &cases.TimestampNone{Val: &timestamppb.Timestamp{Seconds: 123}}, 0},

	{"timestamp - required - valid", &cases.TimestampRequired{Val: &timestamppb.Timestamp{}}, 0},
	{"timestamp - required - invalid", &cases.TimestampRequired{Val: nil}, 1},

	{"timestamp - const - valid", &cases.TimestampConst{Val: &timestamppb.Timestamp{Seconds: 3}}, 0},
	{"timestamp - const - valid (empty)", &cases.TimestampConst{}, 0},
	{"timestamp - const - invalid", &cases.TimestampConst{Val: &timestamppb.Timestamp{Nanos: 3}}, 1},

	{"timestamp - lt - valid", &cases.TimestampLT{Val: &timestamppb.Timestamp{Seconds: -1}}, 0},
	{"timestamp - lt - valid (empty)", &cases.TimestampLT{}, 0},
	{"timestamp - lt - invalid (equal)", &cases.TimestampLT{Val: &timestamppb.Timestamp{}}, 1},
	{"timestamp - lt - invalid", &cases.TimestampLT{Val: &timestamppb.Timestamp{Seconds: 1}}, 1},

	{"timestamp - lte - valid", &cases.TimestampLTE{Val: &timestamppb.Timestamp{}}, 0},
	{"timestamp - lte - valid (empty)", &cases.TimestampLTE{}, 0},
	{"timestamp - lte - valid (equal)", &cases.TimestampLTE{Val: &timestamppb.Timestamp{Seconds: 1}}, 0},
	{"timestamp - lte - invalid", &cases.TimestampLTE{Val: &timestamppb.Timestamp{Seconds: 1, Nanos: 1}}, 1},

	{"timestamp - gt - valid", &cases.TimestampGT{Val: &timestamppb.Timestamp{Seconds: 1}}, 0},
	{"timestamp - gt - valid (empty)", &cases.TimestampGT{}, 0},
	{"timestamp - gt - invalid (equal)", &cases.TimestampGT{Val: &timestamppb.Timestamp{Nanos: 1000}}, 1},
	{"timestamp - gt - invalid", &cases.TimestampGT{Val: &timestamppb.Timestamp{}}, 1},

	{"timestamp - gte - valid", &cases.TimestampGTE{Val: &timestamppb.Timestamp{Seconds: 3}}, 0},
	{"timestamp - gte - valid (empty)", &cases.TimestampGTE{}, 0},
	{"timestamp - gte - valid (equal)", &cases.TimestampGTE{Val: &timestamppb.Timestamp{Nanos: 1000000}}, 0},
	{"timestamp - gte - invalid", &cases.TimestampGTE{Val: &timestamppb.Timestamp{Seconds: -1}}, 1},

	{"timestamp - gt & lt - valid", &cases.TimestampGTLT{Val: &timestamppb.Timestamp{Nanos: 1000}}, 0},
	{"timestamp - gt & lt - valid (empty)", &cases.TimestampGTLT{}, 0},
	{"timestamp - gt & lt - invalid (above)", &cases.TimestampGTLT{Val: &timestamppb.Timestamp{Seconds: 1000}}, 1},
	{"timestamp - gt & lt - invalid (below)", &cases.TimestampGTLT{Val: &timestamppb.Timestamp{Seconds: -1000}}, 1},
	{"timestamp - gt & lt - invalid (max)", &cases.TimestampGTLT{Val: &timestamppb.Timestamp{Seconds: 1}}, 1},
	{"timestamp - gt & lt - invalid (min)", &cases.TimestampGTLT{Val: &timestamppb.Timestamp{}}, 1},

	{"timestamp - exclusive gt & lt - valid (empty)", &cases.TimestampExLTGT{}, 0},
	{"timestamp - exclusive gt & lt - valid (above)", &cases.TimestampExLTGT{Val: &timestamppb.Timestamp{Seconds: 2}}, 0},
	{"timestamp - exclusive gt & lt - valid (below)", &cases.TimestampExLTGT{Val: &timestamppb.Timestamp{Seconds: -1}}, 0},
	{"timestamp - exclusive gt & lt - invalid", &cases.TimestampExLTGT{Val: &timestamppb.Timestamp{Nanos: 1000}}, 1},
	{"timestamp - exclusive gt & lt - invalid (max)", &cases.TimestampExLTGT{Val: &timestamppb.Timestamp{Seconds: 1}}, 1},
	{"timestamp - exclusive gt & lt - invalid (min)", &cases.TimestampExLTGT{Val: &timestamppb.Timestamp{}}, 1},

	{"timestamp - gte & lte - valid", &cases.TimestampGTELTE{Val: &timestamppb.Timestamp{Seconds: 60, Nanos: 1}}, 0},
	{"timestamp - gte & lte - valid (empty)", &cases.TimestampGTELTE{}, 0},
	{"timestamp - gte & lte - valid (max)", &cases.TimestampGTELTE{Val: &timestamppb.Timestamp{Seconds: 3600}}, 0},
	{"timestamp - gte & lte - valid (min)", &cases.TimestampGTELTE{Val: &timestamppb.Timestamp{Seconds: 60}}, 0},
	{"timestamp - gte & lte - invalid (above)", &cases.TimestampGTELTE{Val: &timestamppb.Timestamp{Seconds: 3600, Nanos: 1}}, 1},
	{"timestamp - gte & lte - invalid (below)", &cases.TimestampGTELTE{Val: &timestamppb.Timestamp{Seconds: 59}}, 1},

	{"timestamp - gte & lte - valid (empty)", &cases.TimestampExGTELTE{}, 0},
	{"timestamp - exclusive gte & lte - valid (above)", &cases.TimestampExGTELTE{Val: &timestamppb.Timestamp{Seconds: 3601}}, 0},
	{"timestamp - exclusive gte & lte - valid (below)", &cases.TimestampExGTELTE{Val: &timestamppb.Timestamp{}}, 0},
	{"timestamp - exclusive gte & lte - valid (max)", &cases.TimestampExGTELTE{Val: &timestamppb.Timestamp{Seconds: 3600}}, 0},
	{"timestamp - exclusive gte & lte - valid (min)", &cases.TimestampExGTELTE{Val: &timestamppb.Timestamp{Seconds: 60}}, 0},
	{"timestamp - exclusive gte & lte - invalid", &cases.TimestampExGTELTE{Val: &timestamppb.Timestamp{Seconds: 61}}, 1},

	{"timestamp - lt now - valid", &cases.TimestampLTNow{Val: &timestamppb.Timestamp{}}, 0},
	{"timestamp - lt now - valid (empty)", &cases.TimestampLTNow{}, 0},
	{"timestamp - lt now - invalid", &cases.TimestampLTNow{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 7200}}, 1},

	{"timestamp - gt now - valid", &cases.TimestampGTNow{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 7200}}, 0},
	{"timestamp - gt now - valid (empty)", &cases.TimestampGTNow{}, 0},
	{"timestamp - gt now - invalid", &cases.TimestampGTNow{Val: &timestamppb.Timestamp{}}, 1},

	{"timestamp - within - valid", &cases.TimestampWithin{Val: timestamppb.Now()}, 0},
	{"timestamp - within - valid (empty)", &cases.TimestampWithin{}, 0},
	{"timestamp - within - invalid (below)", &cases.TimestampWithin{Val: &timestamppb.Timestamp{}}, 1},
	{"timestamp - within - invalid (above)", &cases.TimestampWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 7200}}, 1},

	{"timestamp - lt now within - valid", &cases.TimestampLTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() - 1800}}, 0},
	{"timestamp - lt now within - valid (empty)", &cases.TimestampLTNowWithin{}, 0},
	{"timestamp - lt now within - invalid (lt)", &cases.TimestampLTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 1800}}, 1},
	{"timestamp - lt now within - invalid (within)", &cases.TimestampLTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() - 7200}}, 1},

	{"timestamp - gt now within - valid", &cases.TimestampGTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 1800}}, 0},
	{"timestamp - gt now within - valid (empty)", &cases.TimestampGTNowWithin{}, 0},
	{"timestamp - gt now within - invalid (gt)", &cases.TimestampGTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() - 1800}}, 1},
	{"timestamp - gt now within - invalid (within)", &cases.TimestampGTNowWithin{Val: &timestamppb.Timestamp{Seconds: time.Now().Unix() + 7200}}, 1},
}

var anyCases = []TestCase{
	{"any - none - valid", &cases.AnyNone{Val: &anypb.Any{}}, 0},

	{"any - required - valid", &cases.AnyRequired{Val: &anypb.Any{}}, 0},
	{"any - required - invalid", &cases.AnyRequired{Val: nil}, 1},

	{"any - in - valid", &cases.AnyIn{Val: &anypb.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}, 0},
	{"any - in - valid (empty)", &cases.AnyIn{}, 0},
	{"any - in - invalid", &cases.AnyIn{Val: &anypb.Any{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}, 1},

	{"any - not in - valid", &cases.AnyNotIn{Val: &anypb.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}}, 0},
	{"any - not in - valid (empty)", &cases.AnyNotIn{}, 0},
	{"any - not in - invalid", &cases.AnyNotIn{Val: &anypb.Any{TypeUrl: "type.googleapis.com/google.protobuf.Timestamp"}}, 1},
}

var kitchenSink = []TestCase{
	{"kitchensink - field - valid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Const: "abcd", IntConst: 5, BoolConst: false, FloatVal: &wrapperspb.FloatValue{Value: 1}, DurVal: &durationpb.Duration{Seconds: 3}, TsVal: &timestamppb.Timestamp{Seconds: 17}, FloatConst: 7, DoubleIn: 123, EnumConst: cases.ComplexTestEnum_ComplexTWO, AnyVal: &anypb.Any{TypeUrl: "type.googleapis.com/google.protobuf.Duration"}, RepTsVal: []*timestamppb.Timestamp{{Seconds: 3}}, MapVal: map[int32]string{-1: "a", -2: "b"}, BytesVal: []byte("\x00\x99"), O: &cases.ComplexTestMsg_X{X: "foobar"}}}, 0},
	{"kitchensink - valid (unset)", &cases.KitchenSinkMessage{}, 0},
	{"kitchensink - field - invalid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{}}, 7},
	{"kitchensink - field - embedded - invalid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Another: &cases.ComplexTestMsg{}}}, 14},
	{"kitchensink - field - invalid (transitive)", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{Const: "abcd", BoolConst: true, Nested: &cases.ComplexTestMsg{}}}, 14},
	{"kitchensink - many - all non-message fields invalid", &cases.KitchenSinkMessage{Val: &cases.ComplexTestMsg{BoolConst: true, FloatVal: &wrapperspb.FloatValue{}, TsVal: &timestamppb.Timestamp{}, FloatConst: 8, AnyVal: &anypb.Any{TypeUrl: "asdf"}, RepTsVal: []*timestamppb.Timestamp{{Nanos: 1}}}}, 13},
}

var nestedCases = []TestCase{
	{"nested wkt uuid - field - valid", &cases.WktLevelOne{Two: &cases.WktLevelOne_WktLevelTwo{Three: &cases.WktLevelOne_WktLevelTwo_WktLevelThree{Uuid: "f81d16ef-40e2-40c6-bebc-89aaf5292f9a"}}}, 0},
	{"nested wkt uuid - field - invalid", &cases.WktLevelOne{Two: &cases.WktLevelOne_WktLevelTwo{Three: &cases.WktLevelOne_WktLevelTwo_WktLevelThree{Uuid: "not-a-valid-uuid"}}}, 1},
}
