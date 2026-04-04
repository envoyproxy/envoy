package main

import (
	"flag"
	"log"
	"math"
	"os"
	"runtime"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

func init() {
	log.SetFlags(0)
}

func main() {
	parallelism := flag.Int("parallelism", runtime.NumCPU(), "Number of test cases to run in parallel")
	goFlag := flag.Bool("go", false, "Run go test harness")
	ccFlag := flag.Bool("cc", false, "Run c++ test harness")
	javaFlag := flag.Bool("java", false, "Run java test harness")
	pythonFlag := flag.Bool("python", false, "Run python test harness")
	externalHarnessFlag := flag.String("external_harness", "", "Path to a binary to be executed as an external test harness")
	flag.Parse()

	test_cases := shardTestCases(TestCases)
	start := time.Now()
	harnesses := Harnesses(*goFlag, *ccFlag, *javaFlag, *pythonFlag, *externalHarnessFlag)
	successes, failures, skips := run(*parallelism, harnesses, test_cases)

	log.Printf("Successes: %d | Failures: %d | Skips: %d (%v)",
		successes, failures, skips, time.Since(start))

	if failures > 0 {
		os.Exit(1)
	}
}

func shardTestCases(test_cases []TestCase) []TestCase {
	// Support Bazel test sharding by slicing up the list of test cases if requested.
	shard_count, err := strconv.Atoi(os.Getenv("TEST_TOTAL_SHARDS"))
	if err != nil {
		return test_cases
	}

	shard_index, err := strconv.Atoi(os.Getenv("TEST_SHARD_INDEX"))
	if err != nil {
		return test_cases
	}

	// Bazel expects that the test will create or modify the file with the provided name to show that it supports sharding.
	status_file := os.Getenv("TEST_SHARD_STATUS_FILE")
	if status_file == "" {
		return test_cases
	}

	if file, err := os.Create(status_file); err != nil {
		return test_cases
	} else {
		file.Close()
	}
	shard_length := int(math.Ceil(float64(len(test_cases)) / float64(shard_count)))
	shard_start := shard_index * shard_length
	shard_end := (shard_index + 1) * shard_length
	if shard_end >= len(test_cases) {
		shard_end = len(test_cases)
	}
	return test_cases[shard_start:shard_end]
}

func run(parallelism int, harnesses []Harness, test_cases []TestCase) (successes, failures, skips uint64) {
	wg := new(sync.WaitGroup)
	if parallelism <= 0 {
		panic("Parallelism must be > 0")
	}
	if len(harnesses) == 0 {
		panic("At least one harness must be selected with a flag")
	}
	wg.Add(parallelism)

	in := make(chan TestCase)
	out := make(chan TestResult)
	done := make(chan struct{})

	for i := 0; i < parallelism; i++ {
		go Work(wg, in, out, harnesses)
	}

	go func() {
		for res := range out {
			if res.Skipped {
				atomic.AddUint64(&skips, 1)
			} else if res.OK {
				atomic.AddUint64(&successes, 1)
			} else {
				atomic.AddUint64(&failures, 1)
			}
		}
		close(done)
	}()

	for _, test := range test_cases {
		in <- test
	}
	close(in)

	wg.Wait()
	close(out)
	<-done

	return
}
