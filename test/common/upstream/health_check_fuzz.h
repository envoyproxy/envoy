#include "test/common/upstream/health_checker_impl_test.h"

namespace Envoy {
    class HealthCheckFuzz: public HttpHealthCheckerImplTest {
        HealthCheckFuzz(test::common::upstream::HealthCheckTestCase input);
        void initialize(); //can pipe in proto configs in either/or of both of these methods
        void replay();


        

        //state?
        //Helper function?
    }
}