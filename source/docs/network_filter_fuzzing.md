# Generic network-level filter fuzzers overview

Network filters need to be fuzzed. Filters come in two flavors, each with their own fuzzer. Read filters should be added into the [Generic ReadFilter Fuzzer](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_readfilter_fuzz_test.cc). Write Filters should added into the [Generic WriteFilter Fuzzer](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_writefilter_fuzz_test.cc). Some filters are both read and write filters: They should be added into both fuzzers.

Before adding the new filter into the fuzzers, please make sure the filter is designed to accept untrusted inputs, or ready to be hardened to accept untrusted inputs.


# Add a new ReadFilter into Generic Readfilter Fuzzer
Only one step is needed to add a new filter to the fuzzer:
* In the file [config.bzl](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/config.bzl) the name of the filter has to be added to the `READFILTER_FUZZ_FILTERS` list. The fuzz test will figure the available filters from the factories.
```
READFILTER_FUZZ_FILTERS = [
    "envoy.filters.network.client_ssl_auth",
    "envoy.filters.network.ext_authz",
    "envoy.filters.network.envoy_mobile_http_connection_manager",
    # A dedicated http_connection_manager fuzzer can be found in
    # test/common/http/conn_manager_impl_fuzz_test.cc
    "envoy.filters.network.http_connection_manager",
    "envoy.filters.network.local_ratelimit",
    "envoy.filters.network.rbac",
    # TODO(asraa): Remove when fuzzer sets up connections for TcpProxy properly.
    # "envoy.filters.network.tcp_proxy",
    "the_new_filter_created_by_you", // <---Add the filter name here
]
```

# Add a new WriteFilter into Generic Writefilter Fuzzer
For WriteFilter, the name of the filter must be added into the `WRITEFILTER_FUZZ_FILTERS` list of the file [BUILD](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/BUILD).

# Add test cases into corpus
Good test cases can provide good examples for fuzzers to find more paths in the code, increase the coverage and help find bugs more efficiently.
Each test case is a file under the folder [network_readfilter_corpus](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_readfilter_corpus) or [network_writefilter_corpus](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_writefilter_corpus). It consists of two parts: `config` and `actions`.
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
For more details, see the APIs for [ReadFilter Fuzz Testcase](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_readfilter_fuzz.proto) and  [WriteFilter Fuzz Testcase](https://github.com/envoyproxy/envoy/blob/main/test/extensions/filters/network/common/fuzz/network_writefilter_fuzz.proto).

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
