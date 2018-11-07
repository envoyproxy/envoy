#pragma once

#include "common/common/logger.h"
#include <string>
#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/reservoir.h"
#include <vector>
#include <cstdio>
#include "rapidjson/prettywriter.h"

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {

                /**
                 * Request represents parameters used to make a sampling decision.
                 */
                class SamplingRequest {
                public:
                    /**
                     * Default constructor.
                     */
                    SamplingRequest() : host_(), http_method_(), http_url_() {}

                    /**
                     * Constructor.
                     *
                     * @param host_name The host the request contains.
                     * @param http_method The http method of this request.
                     * @param http_url The url of this request.
                     */
                    SamplingRequest(const std::string& host_name, const std::string& http_method, const std::string& http_url) : host_(host_name),
                                                                                                    http_method_(http_method), http_url_(http_url){}

                    /**
                     * Sets host attribute.
                     */
                    void setHost(const std::string& val) { host_ = val; }

                    /**
                     * Sets http method attribute.
                     */
                    void setHttpMethod(const std::string& val) { http_method_ = val; }

                    /**
                     * Sets http method attribute.
                     */
                    void setHttpUrl(const std::string& val) { http_url_ = val; }

                    /**
                     * @return the host value.
                     */
                    const std::string& host() const { return host_; }

                    /**
                     * @return the http method value.
                     */
                    const std::string& httpMethod() const { return http_method_; }

                    /**
                     * @return the http url value.
                     */
                    const std::string& httpUrl() const { return http_url_; }

                private:
                    std::string host_;
                    std::string http_method_;
                    std::string http_url_;
                };

                /**
                 * SamplingRule class represents the base set of properties that define a sampling rule.
                 */
                class SamplingRule {
                public:
                    /**
                     * Default constructor. Creates an empty sampling rule.
                     */
                    SamplingRule() : host_(), service_name_(), http_method_(), url_path_(), fixed_target_(0), rate_(0) {}

                    /**
                     * Constructor for default sampling rule.
                     */
                    SamplingRule(int fixed_target, float rate);

                    /**
                     * Constructor that initializes a sampling rule with the given attributes.
                     *
                     * @param host_name The host name for which the rule should apply.
                     * @param service_name The service name for which the rule should apply.
                     * @param http_method The http method for which the rule should apply
                     * @param url_path The urlPath for which the rule should apply.
                     * @param fixed_target The number of traces per any given second for which the sampling rule will sample.
                     * @param rate The rate at which the rule should sample, after the fixedTarget has been reached.
                     */
                    SamplingRule(std::string& host_name, std::string& service_name, std::string& http_method,
                            std::string& url_path, int fixed_target, float rate);

                    /**
                     * Sets the service name to set.
                     */
                    void setServiceName(const std::string& service_name) { service_name_ = service_name; }

                    /**
                     * Sets the host name to set.
                     */
                    void setHost(const std::string& host_name) { host_ = host_name; }

                    /**
                     * Sets http method attribute.
                     */
                    void setHttpMethod(const std::string& val) { http_method_ = val; }

                    /**
                     * Sets http method attribute.
                     * @param url_path
                     */
                    void setUrlPath(const std::string& url_path) { url_path_ = url_path; }

                    /**
                     * Sets the fixed target.
                     * @param fixed_target
                     */
                    void setFixedTarget(const int fixed_target) { fixed_target_ = fixed_target;}

                    /**
                     * Sets the rate.
                     * @param rate
                     */
                    void setRate(const float rate) { rate_ = rate;}

                    /**
                     * @return the service name.
                     */
                    const std::string& serviceName() const { return service_name_; }

                    /**
                     * @return the host name.
                     */
                    const std::string& host() const { return host_; }

                    /**
                     * @return the http method value.
                     */
                    const std::string& httpMethod() const { return http_method_; }

                    /**
                     * @return the url path value.
                     */
                    const std::string& urlPath() const { return url_path_; }

                    /**
                     * @return the fixed_target
                     */
                    int fixedTarget() const {return fixed_target_;}

                    /**
                     * @return the rate
                     */
                    float rate() const { return rate_;}

                    /**
                     * @return the reservior
                     */
                    Reservoir& reservoir() { return reservoir_; }

                    /**
                     * Determines whether or not this sampling rule applies to the incoming request based on some of the request's parameters. Any null parameters provided will be considered an implicit match. For example, {@code appliesTo(null, null, null)} will always return {@code true}, for any rule.
                     *
                     * @param request_host The host name for the incoming request.
                     * @param request_path The path from the incoming request.
                     * @param request_method The method used to make the incoming request.
                     * @return whether or not this rule applies to the incoming request.
                     */
                    bool appliesTo(std::string request_host, std::string request_path, std::string request_method);

                private:
                    std::string host_;
                    std::string service_name_;
                    std::string http_method_;
                    std::string url_path_;
                    int fixed_target_ = -1;
                    float rate_ = -1.0f;
                    Reservoir reservoir_;
                };

                /**
                 * SamplingRuleManifest represents a full sampling ruleset, with a list of custom rules and default values for incoming requests.
                 */
                class SamplingRuleManifest {
                public:
                    /**
                     * Create an empty sampling rule manifest.
                     */
                    SamplingRuleManifest() : version_(VERSION_) {}

                    /**
                     * Constructor that initializes a sampling rule manifest with the given attributes.
                     * @param version
                     * @param default_rule
                     * @param rules
                     */
                    SamplingRuleManifest(int version, SamplingRule default_rule, std::vector<SamplingRule> rules) : version_(version),
                               default_rule_(default_rule), rules_(rules) {}
                    /**
                     * Sets the list of rules.
                     * @param rules
                     */
                    void setRules(std::vector<SamplingRule> rules) { rules_ = rules; }

                    /**
                     * Sets the version.
                     * @param version
                     */
                    void setVersion(int version) {version_ = version; }

                    /**
                     * Sets the default rule.
                     * @param rule
                     */
                    void setDefaultRule(SamplingRule rule) {default_rule_ = rule;}

                    /**
                     * @return the list of sampling rules
                     */
                    std::vector<SamplingRule>& rules() { return rules_; }

                    /**
                     * @return the version.
                     */
                    int version() { return version_; }

                    /**
                     * @return the default sampling rule.
                     */
                    SamplingRule& defaultRule() { return default_rule_; }

                    void deSerialize(std::string json_file);

                    static const int VERSION_ = 2;

                private:
                    int version_;
                    SamplingRule default_rule_;
                    std::vector<SamplingRule> rules_;
                };

                /**
                 * Strategy provides an interface for implementing trace sampling strategies.
                 */
                class SamplingStrategy {
                public:
                    /**
                     * Destructor.
                     */
                    virtual ~SamplingStrategy() {}

                    /**
                     * ShouldTrace consults sampling rule set to determine if the given request should be traced or not.
                     */
                    virtual bool shouldTrace(SamplingRequest sampling_request) PURE;
                };

                /**
                 * LocalizedSamplingStrategy implements SamplingStrategy class and performs sampling for traces based on
                 * sampling rules json file customer provided or default one.
                 */
                class LocalizedSamplingStrategy : public SamplingStrategy, Logger::Loggable<Logger::Id::tracing> {
                public:

                    /**
                     * Constructor.
                     * @param json_file
                     */
                    LocalizedSamplingStrategy(std::string json_file);

                    /**
                     * Set default sampling rule manifest
                     * @param val
                     */
                    void setDefaultRuleManifest(SamplingRuleManifest val) { default_rule_manifest_ = val; }

                    /**
                     * Return the default sampling rule manifest.
                     * @return default sampling rule manifest
                     */
                    SamplingRuleManifest& defaultRuleManifest() { return default_rule_manifest_; }

                    /**
                     * Set custom sampling rule manifest
                     * @param val
                     */
                    void setCustomRuleManifest(SamplingRuleManifest& val) { custom_rule_manifest_ = val; }

                    /**
                     * Return the custom sampling rule manifest.
                     * @return custom sampling rule manifest
                     */
                    SamplingRuleManifest& customRuleManifest() { return custom_rule_manifest_; }

                    /**
                     * shouldTrace takes sampling request and then determines whether the request will be traced or not.
                     * @param sampling_request
                     * @return
                     */
                    bool shouldTrace(SamplingRequest sampling_request) override;

                    /**
                     * shouldTraceHelper takes sampling decision based on a sampling rule.
                     * @param rule
                     * @return
                     */
                    bool shouldTraceHelper(SamplingRule& rule);

                private:
                    std::string json_file_;
                    SamplingRuleManifest default_rule_manifest_;
                    SamplingRuleManifest custom_rule_manifest_;
                };

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy
