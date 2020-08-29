#include "test/common/upstream/health_checker_impl_test.h"

namespace Envoy {
    class HealthCheckFuzz: public Upstream::HttpHealthCheckerImplTest {
        public:
            HealthCheckFuzz();
            void initialize(test::common::upstream::HealthCheckTestCase input); //can pipe in proto configs in either/or of both of these methods
            void replay();


        private:
            void respond();
            void expectStreamCreate();

        //state?
        //Helper function?
    }
}