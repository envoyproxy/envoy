#include "extensions/tracers/xray/sampling.h"
#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/reservoir.h"
#include "common/http/exception.h"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <fstream>
#include <sstream>

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                SamplingRule::SamplingRule(int fixed_target, float rate) {
                    fixed_target_ = fixed_target;
                    rate_ = rate;
                    reservoir_ = Reservoir(fixed_target);
                }

                SamplingRule::SamplingRule(std::string& host_name, std::string& service_name, std::string& http_method,
                        std::string& url_path, int fixed_target, float rate) {
                    host_ = host_name;
                    service_name_ = service_name;
                    http_method_ = http_method;
                    url_path_ = url_path;
                    fixed_target_ = fixed_target;
                    rate_ = rate;
                    reservoir_ = Reservoir(fixed_target);
                }

                bool SamplingRule::appliesTo(std::string request_host, std::string request_path, std::string request_method) {
                    return (request_host.empty() || Util::wildcardMatch(host_, request_host)) &&
                           (request_path.empty() || Util::wildcardMatch(url_path_, request_path)) &&
                           (request_method.empty() || Util::wildcardMatch(http_method_, request_method));
                }

                void SamplingRuleManifest::deSerialize(std::string json_file) {
                    std::stringstream ss;
                    std::ifstream file(json_file);
                    if (file) {
                        ss << file.rdbuf();
                        file.close();
                    } else {
                        throw EnvoyException("Unable to open custom sampling rule json file.");
                    }

                    rapidjson::Document document;
                    if (document.Parse<0>(ss.str().c_str()).HasParseError())
                        throw EnvoyException("Custom sampling rule json file parsing error.");

                    if (document.HasMember("version")) {
                        if (VERSION_ != document["version"].GetInt()) {
                            throw EnvoyException("Invalid Custom Sampling Rule Json file: invalid version number");
                        }
                    } else {
                        throw EnvoyException("Invalid Custom Sampling Rule Json file: missing version.");
                    }

                    if(document.HasMember("default") && document["default"].IsObject()) {
                        const rapidjson::Value &default_rule = document["default"];
                        SamplingRule default_sampling_rule;
                        for (rapidjson::Value::ConstMemberIterator m = default_rule.MemberBegin(); m != default_rule.MemberEnd(); ++m) {
                            std::string key = m->name.GetString();
                            if(key == "fixed_target" && m->value.IsInt()) {
                                default_sampling_rule.setFixedTarget(m->value.GetInt());
                            } else {
                                throw EnvoyException("Invalid Custom Sampling Rule Json file: missing fixed_target in default rule.");
                            }

                            if(key == "rate" && m->value.IsFloat()) {
                                default_sampling_rule.setFixedTarget(m->value.GetFloat());
                            } else {
                                throw EnvoyException("Invalid Custom Sampling Rule Json file: missing rate in default rule.");
                            }
                        }

                        setDefaultRule(default_sampling_rule);
                    } else {
                        throw EnvoyException("Invalid Custom Sampling Rule Json file: missing default rule.");
                    }

                    if(document.HasMember("rules") && document["rules"].IsArray()) {
                        std::vector<SamplingRule> custom_rules;
                        const rapidjson::Value &rules = document["rules"];
                        for (rapidjson::SizeType i = 0; i < rules.Size(); i++) {
                            SamplingRule rule;
                            for (rapidjson::Value::ConstMemberIterator m = rules[i].MemberBegin(); m != rules[i].MemberEnd(); ++m) {
                                std::string key = m->name.GetString();
                                if (key == "host" && m->value.IsString()) {
                                    rule.setHost(m->value.GetString());
                                } else if (key == "service_name" && m->value.IsString()) {
                                    rule.setServiceName(m->value.GetString());
                                } else if (key == "http_method" && m->value.IsString()) {
                                    rule.setHttpMethod(m->value.GetString());
                                } else if (key == "url_path" && m->value.IsString()) {
                                    rule.setHttpMethod(m->value.GetString());
                                } else if (key == "fixed_target" && m->value.IsInt()) {
                                    rule.setFixedTarget(m->value.GetInt());
                                } else if (key == "rate" && m->value.IsFloat()) {
                                   rule.setFixedTarget(m->value.GetFloat());
                                }
                            }
                            custom_rules.push_back(rule);
                        }
                        setRules(custom_rules);
                    }
                }

                LocalizedSamplingStrategy::LocalizedSamplingStrategy(std::string json_file) {
                    if (json_file.empty()) {
                        ENVOY_LOG(info, "unable to parse empty json file. falling back to default rule set.");
                        std::vector<SamplingRule> empty_rules;
                        SamplingRule default_rule = SamplingRule(1, 0.05);
                        setDefaultRuleManifest(SamplingRuleManifest(SamplingRuleManifest::VERSION_, default_rule, empty_rules));
                    } else {
                        // Parse the json file that customer give
                        SamplingRuleManifest manifest;
                        manifest.deSerialize(json_file);
                        setCustomRuleManifest(manifest);
                    }
                }

                bool LocalizedSamplingStrategy::shouldTrace(SamplingRequest request) {
                    ENVOY_LOG(debug, "determining ShouldTrace decision for: host: {} path: {} method: {}", request.host(), request.httpUrl(), request.httpMethod());
//                    SamplingRule *first_applicable_rule;
//                    if (!custom_rule_manifest_.rules().empty()) {
//                        std::vector<SamplingRule> rules = custom_rule_manifest_.rules();
//                        for (std::vector<SamplingRule>::iterator it = rules.begin(); it != rules.end(); ++it) {
//                            if (it->appliesTo(request.host(), request.httpUrl(), request.httpMethod())) {
//                                first_applicable_rule = &*it;
//                                break;
//                            }
//                        }
//                    }
//
//                    if (first_applicable_rule != NULL) {
//                        return shouldTraceHelper(*first_applicable_rule);
//                    }

                    return shouldTraceHelper(default_rule_manifest_.defaultRule());
                }

                bool LocalizedSamplingStrategy::shouldTraceHelper(SamplingRule& rule) {
                    if (rule.reservoir().take()) {
                        return true;
                    } else {
                        return Util::generateRandomDouble() < rule.rate();
                    }
                }

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy