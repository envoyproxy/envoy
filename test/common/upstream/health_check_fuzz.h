#pragma once
#include "test/common/upstream/health_checker_impl_test.h"
#include "test/common/upstream/health_check_fuzz.pb.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

    class HealthCheckFuzz: public HttpHealthCheckerImplTest {
        public:
            HealthCheckFuzz();
            void initialize(test::common::upstream::HealthCheckTestCase input); //can pipe in proto configs in either/or of both of these methods


        private:
            void respondHeaders(test::fuzz::Headers headers, std::string status);
            void streamCreate();
            std::string constructYamlFromProtoInput(test::common::upstream::HealthCheckTestCase input);
            void TestBody() override {
                
            }

            void replay(test::common::upstream::HealthCheckTestCase input);

        //state?
        //Helper function?
    };

}
}
