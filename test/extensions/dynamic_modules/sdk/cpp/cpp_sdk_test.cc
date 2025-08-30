
#include "source/extensions/dynamic_modules/sdk/cpp/src/cpp_sdk_impl.h"
#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/dynamic_modules/abi_version.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/test_common/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gtest/gtest.h"

class HttpFilterInterfaceImpl : public HttpFilterInterface
{
public:
    int OnRequestHeaders(const EnvoyHttpFilter* e, bool endOfStream) override
    {
        ++m_OnRequestHeadersCalled;
        if (m_OnRequestHeadersFunc) {
            return m_OnRequestHeadersFunc(e, endOfStream);   
        }

        return -1; 
    }

    int OnRequestBody(const EnvoyHttpFilter* e, bool endOfStream) override
    {
        ++m_OnRequestBodyCalled;
        if (m_OnRequestBodyFunc) {
            return m_OnRequestBodyFunc(e, endOfStream);
        }

        return -1;
    }

	int OnResponseHeaders(const EnvoyHttpFilter* e, bool endOfStream) override
    {
        ++m_OnResponseHeadersCalled;
        if (m_OnResponseHeadersFunc) {
            return m_OnResponseHeadersFunc(e, endOfStream);
        }

        return -1;
    }

	int OnResponseBody(const EnvoyHttpFilter* e, bool endOfStream) override
    {
        ++m_OnResponseBodyCalled;
        if (m_OnResponseBodyFunc) {
            return m_OnResponseBodyFunc(e, endOfStream);
        }

        return -1;
    }

    int OnRequestTrailers(const EnvoyHttpFilter* e) override
    {
        ++m_OnRequestTrailersCalled;
        if (m_OnRequestTrailersFunc) {
            return m_OnRequestTrailersFunc(e);
        }

        return -1;
    }

    int OnResponseTrailers(const EnvoyHttpFilter* e) override
    {
        ++m_OnResponseTrailersCalled;
        if (m_OnResponseTrailersFunc) {
            return m_OnResponseTrailersFunc(e);
        }

        return -1;
    }

    void OnStreamComplete(const EnvoyHttpFilter* e) override
    {
        ++m_OnStreamCompleteCalled;
        if (m_OnStreamCompleteFunc) {
            m_OnStreamCompleteFunc(e);
        }
    }

	void Destroy() override
    {
        ++m_DestroyCalled;
        if (m_DestroyFunc) {
            m_DestroyFunc();
        }
    }

    void reset()
    {
        m_OnRequestHeadersCalled = 0;
        m_OnRequestBodyCalled = 0;
        m_OnResponseHeadersCalled = 0;
        m_OnResponseBodyCalled = 0;
        m_OnRequestTrailersCalled = 0;      
        m_OnResponseTrailersCalled = 0;
        m_OnStreamCompleteCalled = 0;
        m_DestroyCalled = 0;

        m_OnRequestHeadersFunc = nullptr;
        m_OnRequestBodyFunc = nullptr;
        m_OnResponseHeadersFunc = nullptr;
        m_OnResponseBodyFunc = nullptr;
        m_OnRequestTrailersFunc = nullptr;
        m_OnResponseTrailersFunc = nullptr;
        m_OnStreamCompleteFunc = nullptr;
        m_DestroyFunc = nullptr;
    }

    int m_OnRequestHeadersCalled = 0;
    int m_OnRequestBodyCalled = 0;
    int m_OnResponseHeadersCalled = 0;
    int m_OnResponseBodyCalled = 0;
    int m_OnRequestTrailersCalled = 0;      
    int m_OnResponseTrailersCalled = 0;
    int m_OnStreamCompleteCalled = 0;
    int m_DestroyCalled = 0;

    std::function<int(const EnvoyHttpFilter*, bool)> m_OnRequestHeadersFunc;
    std::function<int(const EnvoyHttpFilter*, bool)> m_OnRequestBodyFunc;
    std::function<int(const EnvoyHttpFilter*, bool)> m_OnResponseHeadersFunc;
    std::function<int(const EnvoyHttpFilter*, bool)> m_OnResponseBodyFunc;
    std::function<int(const EnvoyHttpFilter*)> m_OnRequestTrailersFunc;
    std::function<int(const EnvoyHttpFilter*)> m_OnResponseTrailersFunc;            
    std::function<void(const EnvoyHttpFilter*)> m_OnStreamCompleteFunc;
    std::function<void()> m_DestroyFunc;
};


class MockDynamicModule : public Envoy::Extensions::DynamicModules::DynamicModule
{
public:
    MockDynamicModule(void* handle) : Envoy::Extensions::DynamicModules::DynamicModule(handle) {}
};

class MockDynamicModuleHttpFilterConfig : public Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfig
{
public:
    MockDynamicModuleHttpFilterConfig( const absl::string_view filter_name, const absl::string_view filter_config,
        Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) : 
            DynamicModuleHttpFilterConfig(filter_name, filter_config, std::move(dynamic_module)) 
    {
        // Set up safe function pointers to avoid segfault in destructor
        on_http_filter_config_destroy_ = [](envoy_dynamic_module_type_http_filter_config_module_ptr) {
            // Safe no-op destroy function for test
        };
        in_module_config_ = nullptr; // No actual module config in test
    }   
};

struct HttpCallbacksTestData 
{
    std::unique_ptr<HttpFilterInterfaceImpl> filter_envoy_ptr;
    std::unique_ptr<EnvoyHttpFilterImpl>     filter_module_ptr;

    Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter* module_http_filter_ptr;

    HttpCallbacksTestData(std::unique_ptr<HttpFilterInterfaceImpl> filter_envoy,
                          std::unique_ptr<EnvoyHttpFilterImpl> filter_module,
                          Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter* module_http_filter)
        : filter_envoy_ptr(std::move(filter_envoy)), filter_module_ptr(std::move(filter_module)),
          module_http_filter_ptr(module_http_filter) {}
};

HttpCallbacksTestData setupCallbacksTestData() 
{
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok());

    auto configPtr =
        std::shared_ptr<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfig>(
            new MockDynamicModuleHttpFilterConfig("test", "test", std::move(dynamic_module.value())));

    auto filter_envoy_ptr = std::make_unique<HttpFilterInterfaceImpl>();
    
    // Create the module config using ABI
    envoy_dynamic_module_type_http_filter_config_module_ptr module_config_ptr =
        envoy_dynamic_module_on_http_filter_config_new(configPtr.get(), "test", 4, "test", 4);
    EXPECT_TRUE(module_config_ptr != nullptr);
    
    // Create a DynamicModuleHttpFilter for the test
    auto module_http_filter_ptr = new Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter(configPtr);
    
    // Create the filter using ABI - pass the DynamicModuleHttpFilter as the filter_envoy_ptr
    // since that's what the ABI callbacks expect
    envoy_dynamic_module_type_http_filter_module_ptr abi_filter_ptr = 
        envoy_dynamic_module_on_http_filter_new(module_config_ptr, module_http_filter_ptr);
    EXPECT_TRUE(abi_filter_ptr != nullptr);
    
    auto filter_module_ptr = std::unique_ptr<EnvoyHttpFilterImpl>(
        reinterpret_cast<EnvoyHttpFilterImpl*>(const_cast<void*>(abi_filter_ptr)));

    return {std::move(filter_envoy_ptr), std::move(filter_module_ptr), module_http_filter_ptr};
}

TEST(CppSdkCallbacksTest, HttpFilterNew) 
{
    Envoy::TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      Envoy::TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

    absl::StatusOr<Envoy::Extensions::DynamicModules::DynamicModulePtr> module = Envoy::Extensions::DynamicModules::newDynamicModuleByName("no_op", false);
    EXPECT_TRUE(module.ok());

    auto configPtr = Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig("test", "test", std::move(module.value()));

    envoy_dynamic_module_type_http_filter_config_envoy_ptr envoy_config = static_cast<envoy_dynamic_module_type_http_filter_config_envoy_ptr>(configPtr.value().get());
    ASSERT_TRUE(envoy_config != nullptr);

    auto module_http_filter_ptr = new Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter(configPtr.value());

    const char* name = "test";
    size_t name_size = std::strlen(name);

    const char* config_ptr = "test_config";
    size_t config_sz = std::strlen(config_ptr);

    envoy_dynamic_module_type_http_filter_config_module_ptr module_config_ptr =
        envoy_dynamic_module_on_http_filter_config_new(envoy_config, name, name_size, config_ptr, config_sz);

    ASSERT_TRUE(module_config_ptr != nullptr);
    const EnvoyFilterConfig* config = reinterpret_cast<const EnvoyFilterConfig*>(module_config_ptr);
    ASSERT_EQ(config->m_name, "test");
    ASSERT_EQ(config->m_raw,   envoy_config);

    envoy_dynamic_module_type_http_filter_module_ptr module_ptr = 
        envoy_dynamic_module_on_http_filter_new(module_config_ptr, module_http_filter_ptr);
    ASSERT_TRUE(module_ptr != nullptr);
    const EnvoyHttpFilterImpl* filter_module = reinterpret_cast<const EnvoyHttpFilterImpl*>(module_ptr);
    ASSERT_TRUE(filter_module != nullptr);
    ASSERT_EQ(filter_module->get_filter_config(), module_config_ptr);
    ASSERT_EQ(filter_module->get_module_ptr(), module_http_filter_ptr);

    auto init_version = envoy_dynamic_module_on_program_init();
    ASSERT_STREQ(init_version, Envoy::Extensions::DynamicModules::kAbiVersion);

    envoy_dynamic_module_on_http_filter_config_destroy(module_config_ptr);
    Envoy::TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST(CppSdkCallbacksTest, HttpRequestHeaders) 
{
    auto testData = setupCallbacksTestData();
    
    auto request_headers = Envoy::Http::TestRequestHeaderMapImpl{{":method", "POST"}, {"x-test", "test-data"}};
    testData.module_http_filter_ptr->request_headers_ = &request_headers;
    ASSERT_EQ(testData.module_http_filter_ptr->request_headers_->size(), 2);

    testData.filter_envoy_ptr->m_OnRequestHeadersFunc = [](const EnvoyHttpFilter* e, bool endOfStream) -> int {
        EXPECT_TRUE(e != nullptr);
        EXPECT_FALSE(endOfStream);
        
        std::unordered_map<std::string_view, std::string_view> headers = e->get_request_headers();
        EXPECT_EQ(headers.size(), 2);
        EXPECT_EQ(headers.at(":method"), "POST");
        EXPECT_EQ(headers.at("x-test"), "test-data");

        EXPECT_EQ(e->get_request_header_value(":method").value(), "POST");
        EXPECT_EQ(e->get_request_header_value("x-test").value(), "test-data");

        e->set_request_header("x-new-header", "new-value");
        EXPECT_EQ(e->get_request_header_value("x-new-header").value(), "new-value");

        e->remove_request_header("x-test");
        EXPECT_FALSE(e->get_request_header_value("x-test").has_value());

        EXPECT_EQ(e->get_request_header_values("x-new-header").size(), 1);

        return envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_request_headers(testData.filter_envoy_ptr.get(), 
                            testData.filter_module_ptr.get(), false);

    ASSERT_EQ(testData.filter_envoy_ptr->m_OnRequestHeadersCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue);
}

TEST(CppSdkCallbacksTest, HttpRequestBody) 
{
    auto testData = setupCallbacksTestData();
    Envoy::Buffer::OwnedImpl request_body;
    request_body.add("test-body-data"); 
    Envoy::Http::MockStreamDecoderFilterCallbacks decoder_callbacks;

    auto n = request_body.getRawSlices(std::nullopt);
    std::cout<<"Raw slices count: " << n.size() << "\n";
    for (const auto& slice : n) {
        std::cout << "  Slice data: " << std::string(static_cast<const char*>(slice.mem_), slice.len_) << "\n";
    }   

    testData.module_http_filter_ptr->current_request_body_ = &request_body;
    testData.module_http_filter_ptr->decoder_callbacks_ = &decoder_callbacks;

    EXPECT_CALL(decoder_callbacks, decodingBuffer()).WillRepeatedly(testing::Return(&request_body));
    EXPECT_CALL(decoder_callbacks, modifyDecodingBuffer(_))
        .WillRepeatedly(testing::Invoke([&request_body](std::function<void(Envoy::Buffer::Instance&)> func) {
            func(request_body);
        }));
    testData.filter_envoy_ptr->m_OnRequestBodyFunc = [](const EnvoyHttpFilter* e, bool endOfStream) -> int {
        EXPECT_TRUE(e != nullptr);
        EXPECT_FALSE(endOfStream);
        
        EXPECT_EQ(e->get_request_body()[0], "test-body-data");
        EXPECT_EQ(e->get_request_body().size(), 1);

        e->append_request_body('n');
        EXPECT_EQ(e->get_request_body()[0], "test-body-datan");

        e->drain_request_body(5);
        EXPECT_EQ(e->get_request_body()[0], "body-datan");

        return envoy_dynamic_module_type_on_http_filter_request_body_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_request_body(testData.filter_envoy_ptr.get(), testData.filter_module_ptr.get(), false);

    ASSERT_EQ(testData.filter_envoy_ptr->m_OnRequestBodyCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_request_body_status_Continue);
}

TEST(CppSdkCallbacksTest, HttpRequestTrailers) 
{
    auto testData = setupCallbacksTestData();

    auto request_trailers = Envoy::Http::TestRequestTrailerMapImpl{{":dur", "1.1"}, {"x-test", "test-data"}};
    testData.module_http_filter_ptr->request_trailers_ = &request_trailers;
    ASSERT_EQ(testData.module_http_filter_ptr->request_trailers_->size(), 2);

    testData.filter_envoy_ptr->m_OnRequestTrailersFunc = [](const EnvoyHttpFilter* e) -> int {
        EXPECT_TRUE(e != nullptr);

        std::unordered_map<std::string_view, std::string_view> trailers = e->get_request_trailers();
        EXPECT_EQ(trailers.size(), 2);
        EXPECT_EQ(trailers.at(":dur"), "1.1");
        EXPECT_EQ(trailers.at("x-test"), "test-data");

        EXPECT_EQ(e->get_request_trailer_value(":dur").value(),   "1.1");
        EXPECT_EQ(e->get_request_trailer_value("x-test").value(), "test-data");

        e->set_request_trailer("x-new-header", "new-value");
        EXPECT_EQ(e->get_request_trailer_value("x-new-header").value(), "new-value");

        e->remove_request_trailer("x-test");
        EXPECT_FALSE(e->get_request_trailer_value("x-test").has_value());

        EXPECT_EQ(e->get_request_trailer_values("x-new-header").size(), 1);

        return envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_request_trailers(testData.filter_envoy_ptr.get(), testData.filter_module_ptr.get());
    ASSERT_EQ(testData.filter_envoy_ptr->m_OnRequestTrailersCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue);
}

TEST(CppSdkCallbacksTest, HttpMetaData) 
{
    auto testData = setupCallbacksTestData();
    Envoy::Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
    Envoy::StreamInfo::MockStreamInfo mock_stream_info;
    envoy::config::core::v3::Metadata test_meta;

    const char* namespace_ptr = "test-nspace";
    absl::string_view namespace_view(namespace_ptr, std::strlen(namespace_ptr));
    auto metadata_namespace = test_meta.mutable_filter_metadata()->emplace(namespace_view, Envoy::ProtobufWkt::Struct{}).first;
    ASSERT_EQ(metadata_namespace->first, "test-nspace");

    testData.module_http_filter_ptr->decoder_callbacks_ = &decoder_callbacks;
   
    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(mock_stream_info));
    EXPECT_CALL(mock_stream_info, dynamicMetadata()).WillRepeatedly(testing::ReturnRef(test_meta));
    testData.filter_envoy_ptr->m_OnRequestHeadersFunc = [](const EnvoyHttpFilter* e, bool endOfStream) -> int {
        EXPECT_TRUE(e != nullptr);
        EXPECT_FALSE(endOfStream);

        EXPECT_TRUE(e->set_dynamic_metadata_number("test-nspace", "test-key", 42));

        auto num = e->get_dynamic_metadata_number("test-nspace", "test-key");
        EXPECT_TRUE(num.has_value());
        EXPECT_EQ(*num, 42);

        return envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
    };

    envoy_dynamic_module_on_http_filter_request_headers(testData.filter_envoy_ptr.get(), 
                            testData.filter_module_ptr.get(), false);

    ASSERT_EQ(testData.filter_envoy_ptr->m_OnRequestHeadersCalled, 1);
}

class TestIntAccessor : public Envoy::StreamInfo::FilterState::Object 
{
public:
  TestIntAccessor(int value) : value_(value) {}

  int access() const { return value_; }

private:
  int value_;
};

TEST(CppSdkCallbacksTest, HttpFilterState) 
{
    auto testData = setupCallbacksTestData();
    Envoy::Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
    Envoy::StreamInfo::MockStreamInfo mock_stream_info;

    mock_stream_info.filter_state_ = std::make_shared<Envoy::StreamInfo::FilterStateImpl>(Envoy::StreamInfo::FilterState::LifeSpan::Request);
    /*mock_stream_info.filter_state_->setData("test", std::make_unique<TestIntAccessor>(1),
                                       Envoy::StreamInfo::FilterState::StateType::Mutable,
                                       Envoy::StreamInfo::FilterState::LifeSpan::FilterChain);*/

    testData.module_http_filter_ptr->decoder_callbacks_ = &decoder_callbacks;
    EXPECT_CALL(decoder_callbacks, sendLocalReply(_, _, _, _, _));
    EXPECT_CALL(decoder_callbacks, encodeHeaders_(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(decoder_callbacks, encodeData(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(decoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(mock_stream_info));
    EXPECT_CALL(mock_stream_info,  filterState()).WillRepeatedly(testing::ReturnRef(mock_stream_info.filter_state_));
    testData.filter_envoy_ptr->m_OnStreamCompleteFunc = [](const EnvoyHttpFilter* e) -> void {
        EXPECT_TRUE(e != nullptr);

        e->send_response(200, {"x-response-header: value"}, "Response body content");

        e->set_filter_state_bytes("test", "1");
        auto state = e->get_filter_state_bytes("test");
        EXPECT_TRUE(state.has_value());
        //EXPECT_EQ(*state, "1");
    };

    envoy_dynamic_module_on_http_filter_stream_complete(testData.filter_envoy_ptr.get(), testData.filter_module_ptr.get());
    ASSERT_EQ(testData.filter_envoy_ptr->m_OnStreamCompleteCalled, 1);
}

TEST(CppSdkCallbacksTest, HttpResponseHeaders) 
{
    auto testData = setupCallbacksTestData();

    auto response_headers = Envoy::Http::TestResponseHeaderMapImpl{{":status", "200"}, {"x-test", "test-data"}};
    testData.module_http_filter_ptr->response_headers_ = &response_headers;
    ASSERT_EQ(testData.module_http_filter_ptr->response_headers_->size(), 2);

    testData.filter_envoy_ptr->m_OnResponseHeadersFunc = [](const EnvoyHttpFilter* e, bool endOfStream) -> int {
        EXPECT_TRUE(e != nullptr);
        EXPECT_FALSE(endOfStream);

        std::unordered_map<std::string_view, std::string_view> headers = e->get_response_headers();
        EXPECT_EQ(headers.size(), 2);
        EXPECT_EQ(headers.at(":status"), "200");
        EXPECT_EQ(headers.at("x-test"), "test-data");

        EXPECT_EQ(e->get_response_header_value(":status").value(), "200");
        EXPECT_EQ(e->get_response_header_value("x-test").value(), "test-data");

        e->set_response_header("x-new-header", "new-value");
        EXPECT_EQ(e->get_response_header_value("x-new-header").value(), "new-value");

        return envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_response_headers(testData.filter_envoy_ptr.get(), 
                            testData.filter_module_ptr.get(), false);

    ASSERT_EQ(testData.filter_envoy_ptr->m_OnResponseHeadersCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue);
}

TEST(CppSdkCallbacksTest, HttpResponseBody) 
{
    auto testData = setupCallbacksTestData();
    Envoy::Buffer::OwnedImpl response_body;
    response_body.add("test-body-data"); 
    Envoy::Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
    testData.module_http_filter_ptr->current_response_body_ = &response_body;
    testData.module_http_filter_ptr->encoder_callbacks_ = &encoder_callbacks;

    EXPECT_CALL(encoder_callbacks, encodingBuffer()).WillRepeatedly(testing::Return(&response_body));
    EXPECT_CALL(encoder_callbacks, modifyEncodingBuffer(_))
        .WillRepeatedly(testing::Invoke([&response_body](std::function<void(Envoy::Buffer::Instance&)> func) {
            func(response_body);
        }));
    testData.filter_envoy_ptr->m_OnResponseBodyFunc = [](const EnvoyHttpFilter* e, bool endOfStream) -> int {
        EXPECT_TRUE(e != nullptr);
        EXPECT_FALSE(endOfStream);

        EXPECT_EQ(e->get_response_body()[0], "test-body-data");
        EXPECT_EQ(e->get_response_body().size(), 1);

        e->append_response_body('n');
        EXPECT_EQ(e->get_response_body()[0], "test-body-datan");

        e->drain_response_body(5);
        EXPECT_EQ(e->get_response_body()[0], "body-datan");

        return envoy_dynamic_module_type_on_http_filter_response_body_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_response_body(testData.filter_envoy_ptr.get(), 
                            testData.filter_module_ptr.get(), false);

    ASSERT_EQ(testData.filter_envoy_ptr->m_OnResponseBodyCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_response_body_status_Continue);
}

TEST(CppSdkCallbacksTest, HttpResponseTrailers) 
{
    auto testData = setupCallbacksTestData();

    auto response_trailers = Envoy::Http::TestResponseTrailerMapImpl{{"x-test", "test-data"}};
    testData.module_http_filter_ptr->response_trailers_ = &response_trailers;
    ASSERT_EQ(testData.module_http_filter_ptr->response_trailers_->size(), 1);

    testData.filter_envoy_ptr->m_OnResponseTrailersFunc = [](const EnvoyHttpFilter* e) -> int {
        EXPECT_TRUE(e != nullptr);

        std::unordered_map<std::string_view, std::string_view> trailers = e->get_response_trailers();
        EXPECT_EQ(trailers.size(), 1);
        EXPECT_EQ(trailers.at("x-test"), "test-data");

        EXPECT_EQ(e->get_response_trailer_value("x-test").value(), "test-data");
        e->set_response_trailer("x-new-header", "new-value");
        EXPECT_EQ(e->get_response_trailer_value("x-new-header").value(), "new-value");  

        e->remove_response_trailer("x-test");
        EXPECT_FALSE(e->get_response_trailer_value("x-test").has_value());

        return envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue;
    };

    auto ret = envoy_dynamic_module_on_http_filter_response_trailers(testData.filter_envoy_ptr.get(), 
                            testData.filter_module_ptr.get());
    ASSERT_EQ(testData.filter_envoy_ptr->m_OnResponseTrailersCalled, 1);
    ASSERT_EQ(ret, envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue);
}