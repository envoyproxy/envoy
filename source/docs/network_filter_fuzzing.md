# Generic network-level filter fuzzers overview

Network filters need to be fuzzed. Filters come in two flavors, each with their own fuzzer. Read filters should be added into the [Generic ReadFilter Fuzzer](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_readfilter_fuzz_test.cc). Write Filters should added into the [Generic WriteFilter Fuzzer](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_writefilter_fuzz_test.cc). Some filters are both raed and write filters: They should be added into both fuzzers.

Before adding the new filter into the fuzzers, please make sure the filter is designed to accept untrusted inputs, or ready to be hardened to accept untrusted inputs.


# Add a new ReadFilter into Generic Readfilter Fuzzer
## Step1. Make sure the filter can be linked into the fuzzer
There are two ways to link it into the fuzzer. 
* [Recommended] In the file [extensions_build_config.bzl](https://github.com/envoyproxy/envoy/blob/master/source/extensions/extensions_build_config.bzl), the name of the filter should have a prefix `envoy.filters.network`. If it has such a prefix, the filter will be automatically linked into Generic ReadFilter Fuzzer.
* [Not recommended]If for some reasons the filter's name doesn't have such a prefix, the config of the filter must be added into the `deps` field of `network_readfilter_fuzz_test` module in the file [BUILD](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/BUILD).
### Step2. Add the filter name into supported_filter_names
In [uber_per_readfilter.cc](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/uber_per_readfilter.cc), add the filter name into the vector `supported_filter_names` in method `UberFilterFuzzer::filterNames()`.
```
const std::vector<absl::string_view> supported_filter_names = {
...
NetworkFilterNames::get().ExtAuthorization, NetworkFilterNames::get().TheNewFilterCreatedByYou,
...
};
```

# Add a new WriteFilter into Generic Writefilter Fuzzer
## Step 1. Make sure the filter can be linked into the fuzzer
For WriteFilter, the config of the filter must be added into the `deps` field of `network_writefilter_fuzz_test` module in the file [BUILD](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/BUILD).
```
envoy_cc_fuzz_test(
    name = "network_writefilter_fuzz_test",
    srcs = ["network_writefilter_fuzz_test.cc"],
    corpus = "network_writefilter_corpus",
    # All Envoy network filters must be linked to the test in order for the fuzzer to pick
    # these up via the NamedNetworkFilterConfigFactory.
    deps = [
        ":uber_writefilter_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/kafka:kafka_broker_config_lib",
        "//source/extensions/filters/network/mongo_proxy:config",
        "//source/extensions/filters/network/mysql_proxy:config",
        "//source/extensions/filters/network/zookeeper_proxy:config",
        "//source/extensions/filters/network/the_new_filter_created_by_you:config", // <---Add the filter config module here
        "//test/config:utility_lib",
    ],
)
```
## Step 2. Add the filter name into supported_filter_names
In [uber_per_writefilter.cc](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/uber_per_writefilter.cc), add the filter name into the vector `supported_filter_names` in method `UberWriteFilterFuzzer::filterNames()`.
```
const std::vector<absl::string_view> supported_filter_names = {
      ...
      NetworkFilterNames::get().ExtAuthorization, NetworkFilterNames::get().TheNewFilterCreatedByYou,
      ...
    };
```

# Add test cases into corpus
Good test cases can provide good examples for fuzzers to find more paths in the code, increase the coverage and help find bugs more efficiently.
Each test case is a file under the folder [network_readfilter_corpus](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_readfilter_corpus) or [network_writefilter_corpus](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_writefilter_corpus). It consists of two parts: `config` and `actions`. 
`config` is the protobuf to instantiate a filter, and `actions` are sequences of actions to take in order to test the filter. 
An example for testing MongoProxy filter:
```
config {
  name: "envoy.filters.network.mongo_proxy"
  typed_config {
    type_url: "type.googleapis.com/envoy.extensions.filters.network.mongo_proxy.v3.MongoProxy"
    value: "\n\001\\\032\t\032\002\020\010\"\003\010\200t \001"
  }
}
actions {
  on_new_connection {
  }
}
actions {
  on_data {
    data: "\120\0\0\0\1\0\0\0\1\0\0\0\324\7\0\0\4\0\0\0\164\145\163\164\56\164\145\163\164\0\24\0\0\0\377\377\377\377\52\0\0\0\2\163\164\162\151\156\147\137\156\145\145\144\137\145\163\143\0\20\0\0\0\173\42\146\157\157\42\72\40\42\142\141\162\12\42\175\0\0"
  }
}
actions {
  advance_time {
    milliseconds: 10000
  }
}
```
* `config.name` is the name of the filter. 
* `config.typed_config.type_url` is the type url of the filter config API. 
* `config.typed_config.value` is the serialized string of the config protobuf, and in C++ we can call`config.SerializeAsString()` to obtain this. This string may contain special characters. Recommend using octal or hexadecimal sequence for the string.
* `actions.on_data.data` (or `actions.on_write.data`) is the buffer parameter `data`(in string format) for testing ReadFilter's method onData() (or for testing WriteFilter's method onWrite()). This string may contain special characters. Recommend using octal or hexadecimal sequence for the string.
* `actions.on_data.end_stream` (or `actions.on_write.end_stream`) is the bool parameter `end_stream` for testing ReadFilter's method onData() (or for testing WriteFilter's method onWrite()).
* `actions.on_new_connection` is an action to call `onNewConnection` method of a ReadFilter.
* `actions.advance_time.milliseconds` is the duration in milliseconds for the simulatedSystemTime to advance by.
For more details, see the APIs for [ReadFilter Fuzz Testcase](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_readfilter_fuzz.proto) and  [WriteFilter Fuzz Testcase](https://github.com/envoyproxy/envoy/blob/master/test/extensions/filters/network/common/fuzz/network_writefilter_fuzz.proto).

## Convert a unit test case to a fuzz test case manually
This section explains an approach to generate a corpus from unit tests. It is an optional step for users who want to generate the highest possible coverage.
Unit test cases usually leads the filter into interesting states. Currently there is no automatic way to convert a unit test case into a fuzz test case. However, there is a way to convert it manually.
We can write a utility function like this and use it to print the `buffer` and `protobuf` as a octal sequence to avoid invisible characters:
```
static std::string toOct(const std::string& source, const std::string& info) {
   std::stringstream ss;
   for (unsigned char c : source) {
     int n=c-'\0';
     ss<<'\\'<<std::oct<<n;
   }
   std::cout<<"info = "<<info<<", string:"<<ss.str()<<std::endl;
   return ss.str();
 }
```
In the unit test code, we temporarily add a function(finally we will remove it) like the above one.
Then we can fill in `config.typed_config.value` with the value returned or printed by 
```toOct(config.SerializeAsString(), "config serialized string: ")``` 
where `config` is the config protobuf in a unit test case.

We can also fill in `actions.on_data.data` or `actions.on_write.data` with the value returned or printed by 
```toOct(buffer.toString(), "buffer:")``` 
where `buffer` is the buffer to pass to `onData()` or `onWrite()` in a unit test case.

Please note that the two fuzzers use the "real input" for fuzzers. If you are using a mock decoder and pass an empty buffer to onData(), that test case won't help cover much code in the fuzzers(but the config protobuf is still helpful).

## Known issues in the fuzzers
* Both of the fuzzers use static helper classes to speed up the fuzz tests. Once there is an issue which is unreproducile by a single testcase, please check the mock objects which are modified by the filters and have states inside (some of them have a vector or a map object inside). Please make sure they are correctly reset or cleared in `UberFilterFuzzer::reset()` or `UberWriteFilterFuzzer::reset()`.
* The fuzzers don't support system calls like file IO or network IO. Please avoid triggering the related function calls by disabling the test case in `UberFilterFuzzer::checkInvalidInputForFuzzer`, or add some mock objects depending on your functionality.
