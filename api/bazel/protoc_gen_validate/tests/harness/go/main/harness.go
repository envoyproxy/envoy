package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"

	"github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go"
	_ "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go"
	_ "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package/go"
	_ "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/yet_another_package/go"
	"github.com/envoyproxy/protoc-gen-validate/tests/harness/go"
	"google.golang.org/protobuf/proto"
)

func main() {
	b, err := ioutil.ReadAll(os.Stdin)
	checkErr(err)

	tc := new(harness.TestCase)
	checkErr(proto.Unmarshal(b, tc))

	msg, err := tc.Message.UnmarshalNew()
	checkErr(err)

	_, isIgnored := msg.(*cases.MessageIgnored)

	vMsg, hasValidate := msg.(interface {
		Validate() error
	})

	vAllMsg, hasValidateAll := msg.(interface {
		ValidateAll() error
	})

	var multierr error
	if isIgnored {
		// confirm that ignored messages don't have a validate method
		if hasValidate {
			checkErr(fmt.Errorf("ignored message %T has Validate() method", msg))
		}
		if hasValidateAll {
			checkErr(fmt.Errorf("ignored message %T has ValidateAll() method", msg))
		}
	} else if !hasValidate {
		checkErr(fmt.Errorf("non-ignored message %T is missing Validate()", msg))
	} else if !hasValidateAll {
		checkErr(fmt.Errorf("non-ignored message %T is missing ValidateAll()", msg))
	} else {
		err = vMsg.Validate()
		multierr = vAllMsg.ValidateAll()
	}
	checkValid(err, multierr)
}

type hasAllErrors interface{ AllErrors() []error }
type hasCause interface{ Cause() error }

func checkValid(err, multierr error) {
	if err == nil && multierr == nil {
		resp(&harness.TestResult{Valid: true})
		return
	}
	if (err != nil) != (multierr != nil) {
		checkErr(fmt.Errorf("different verdict of Validate() [%v] vs. ValidateAll() [%v]", err, multierr))
		return
	}

	// Extract the message from "lazy" Validate(), for comparison with ValidateAll()
	rootCause := err
	for {
		caused, ok := rootCause.(hasCause)
		if !ok || caused.Cause() == nil {
			break
		}
		rootCause = caused.Cause()
	}

	// Retrieve the messages from "extensive" ValidateAll() and compare first one with the "lazy" message
	m, ok := multierr.(hasAllErrors)
	if !ok {
		checkErr(fmt.Errorf("ValidateAll() returned error without AllErrors() method: %#v", multierr))
		return
	}
	reasons := mergeReasons(nil, m)
	if rootCause.Error() != reasons[0] {
		checkErr(fmt.Errorf("different first message, Validate()==%q, ValidateAll()==%q", rootCause.Error(), reasons[0]))
		return
	}

	resp(&harness.TestResult{Reasons: reasons})
}

func mergeReasons(reasons []string, multi hasAllErrors) []string {
	for _, err := range multi.AllErrors() {
		caused, ok := err.(hasCause)
		if ok && caused.Cause() != nil {
			err = caused.Cause()
		}
		multi, ok := err.(hasAllErrors)
		if ok {
			reasons = mergeReasons(reasons, multi)
		} else {
			reasons = append(reasons, err.Error())
		}
	}
	return reasons
}

func checkErr(err error) {
	if err == nil {
		return
	}

	resp(&harness.TestResult{
		Error:   true,
		Reasons: []string{err.Error()},
	})
}

func resp(result *harness.TestResult) {
	if b, err := proto.Marshal(result); err != nil {
		log.Fatalf("could not marshal response: %v", err)
	} else if _, err = os.Stdout.Write(b); err != nil {
		log.Fatalf("could not write response: %v", err)
	}

	os.Exit(0)
}
