#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Reservoir Keeps track of the number of sampled segments within a single second.
                 * This class is implemented to be thread-safe to achieve accurate sampling.
                 */
                class Reservoir{
                public:
                    /**
                    * Copy constructor.
                    */
                    Reservoir(const Reservoir&);

                    /**
                    * Assignment operator.
                    */
                    Reservoir& operator=(const Reservoir& reservoir);

                    /**
                     * Default constructor.
                     */
                    Reservoir() : traces_per_second_(), this_second_(), used_this_second_{0} {};

                    /**
                     * Constructor. Reservoir creates a new reservoir with a specified trace per second
                     * sampling capacity. The maximum supported sampling capacity per second is currently 100,000,000.
                     * @param trace_per_second
                     */
                    Reservoir(const int trace_per_second);

                    /**
                     * take returns true when the reservoir has remaining sampling capacity for the current epoch.
                     * take returns false when the reservoir has no remaining sampling capacity for the current epoch.
                     */
                    bool take();

                    /**
                     * @return traces_per_second_
                     */
                    int tracesPerSecond() const { return traces_per_second_; }

                    /**
                     *
                     * @return this_second_
                     */
                    int thisSecond() const { return this_second_; }

                    /**
                     *
                     * @return used_this_second_
                     */
                    int usedThisSecond() const { return used_this_second_; }

                private:
                    // trace_per_second_ is the number of guaranteed sampled segments.
                    int traces_per_second_;
                    int this_second_;
                    int used_this_second_;
                    std::mutex m_lock;
                };

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy

