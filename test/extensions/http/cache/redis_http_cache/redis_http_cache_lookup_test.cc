#include "gtest/gtest.h"

#include "source/extensions/http/cache/redis_http_cache/redis_http_cache_lookup.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/thread_local_cluster.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace RedisHttpCache {

std::string convertToFlatString(std::string array) {
    // Drop first and the least characters. Those are square brackets.
//    ASSERT_EQ(array[0], ']');

    array = array.substr(2, array.length() - 4);

    auto parts = absl::StrSplit(array, "\", \"");

    return absl::StrJoin(parts, " ");
}

class RedisHttpCacheLookupTest : public ::testing::TestWithParam<std::tuple<std::string, CacheEntryStatus>> {
public:
    RedisHttpCacheLookupTest()
                                    :  tls_slot_(tls_allocator_) {
    thread_local_redis_client_ = std::make_shared<ThreadLocalRedisClient>();;
    tls_slot_.set([&](Event::Dispatcher&) {return thread_local_redis_client_;});

async_client_ = new Tcp::AsyncClient::MockAsyncTcpClient();

EXPECT_CALL(cluster_manager_, getThreadLocalCluster("redis_cluster")).WillOnce(testing::Return(&thread_local_cluster_));
//ON_CALL(thread_local_cluster_, tcpAsyncClient(_, _)).WillByDefault(testing::Return(std::make_unique<Tcp::AsyncTcpClient>((Tcp::AsyncTcpClient*)async_client)));
EXPECT_CALL(thread_local_cluster_, tcpAsyncClient(_, _)).WillOnce(Invoke([&] {
    return Tcp::AsyncTcpClientPtr{async_client_};
    }));
EXPECT_CALL(*async_client_, connect()).WillOnce(testing::Return(true));
EXPECT_CALL(*async_client_, setAsyncTcpClientCallbacks(_));

    request_headers_.setMethod("GET");
request_headers_.setHost("example.com");
request_headers_.setScheme("https");
request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
request_headers_.setPath("/");
    }
 Event:: MockDispatcher dispatcher_;
  Upstream::MockClusterManager cluster_manager_;
  Upstream::MockThreadLocalCluster thread_local_cluster_;
NiceMock<ThreadLocal::MockInstance> tls_allocator_;
                                      ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient> tls_slot_; 
    std::shared_ptr<ThreadLocalRedisClient> thread_local_redis_client_;
Event::SimulatedTimeSystem time_system_;
NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
Http::TestRequestHeaderMapImpl request_headers_;
::envoy::extensions::filters::http::cache::v3::CacheConfig config_;
VaryAllowList vary_allow_list_{config_.allowed_vary_headers(), factory_context_};
Tcp::AsyncClient::MockAsyncTcpClient* async_client_;
};

TEST_P(RedisHttpCacheLookupTest, SendRequestAndReceiveReply) {
                    LookupRequest lookup(request_headers_, time_system_.systemTime(), vary_allow_list_);

  
                                      //ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient> tls_slot_ = ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient>::makeUnique(tls_allocator_); 
                                      //ThreadLocal::TypedSlot</*RedisHttpCache::*/ThreadLocalRedisClient> tls_slot_(tls_allocator_); 

  RedisHttpCacheLookupContext lookup_context(dispatcher_, 
    
//   RedisHttpCache& cache, 
                    cluster_manager_,
                                      tls_slot_,
                    std::move(lookup));

  EXPECT_CALL(*async_client_, write(_,_)).WillOnce(Invoke([&] (Buffer::Instance& buf, bool){
    std::string query = buf.toString();
    ASSERT_FALSE(query.empty());
    // query still contains redis encoded values. Write or find an utility function to convert it
    // to a string.
    //ASSERT_EQ(query, fmt::format("get cache-{}", stableHashKey(lookup.key())));
    
    class TestDecoderCallbacks : public  NetworkFilters::Common::Redis::DecoderCallbacks {
        public:
        void onRespValue(NetworkFilters::Common::Redis::RespValuePtr&& value) override {
            content_ += value->toString();
        }
        std::string getContent() const {return content_;}

        private:
        std::string content_;
    };

    // Use redis decoder to 
  TestDecoderCallbacks callbacks;
  NetworkFilters::Common::Redis::DecoderImpl decoder(callbacks);
    decoder.decode(buf);

    // Verify that proper command is sent to the server.
    ASSERT_EQ(fmt::format(RedisGetHeadersCmd, stableHashKey(lookup_context.lookup().key())), convertToFlatString(callbacks.getContent()));
    
    }));

  lookup_context.getHeaders([expected_status = std::get<1>(GetParam())] (LookupResult&& result, bool /*end_stream*/) {
    // This callback is called when reply from the Redis server is received.
    // Feed different results and check how lookup_context reacts to those inputs.
    ASSERT_EQ(result.cache_entry_status_, expected_status);
    });

  // Now call callback which is invoked when Redis responds.
  // Not sure if giving non-Resp value will actually call callback
  // registered in the decoder.
  // I guess we have to use encoder to create responses. Giving a garbage will most likely not 
  // result in call back registered in lookup_context.
  Buffer::OwnedImpl buf;
  NetworkFilters::Common::Redis::EncoderImpl encoder;
NetworkFilters::Common::Redis::RespValue request;
  std::vector<NetworkFilters::Common::Redis::RespValue> values(1);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  // return empty payload.
  values[0].asString() = std::get<0>(GetParam());//"";//test";//"get";
  request.type(NetworkFilters::Common::Redis::RespType::Array);
  request.asArray().swap(values);
  encoder.encode(request, buf);  
#if 0
  buf.add("test"); // This does not cause the decoder to call callback.
#endif
  thread_local_redis_client_->redis_client_.onData(buf, true); 
}

INSTANTIATE_TEST_SUITE_P(RedisHttpCacheLookupTestSuite, RedisHttpCacheLookupTest,
                         ::testing::Values(
                            std::make_tuple("", CacheEntryStatus::LookupError),
                            std::make_tuple("test", CacheEntryStatus::Unusable)
                        ));


} // namespace RedisHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
