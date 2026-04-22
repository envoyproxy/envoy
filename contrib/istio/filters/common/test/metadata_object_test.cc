#include "envoy/registry/registry.h"

#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Istio {
namespace Common {

using Envoy::Protobuf::util::MessageDifferencer;

TEST(WorkloadMetadataObjectTest, AsString) {
  constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
  WorkloadMetadataObject deploy("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                                "v1alpha3", "", "", WorkloadType::Deployment, identity, "", "");

  WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                             "v1alpha3", "", "", WorkloadType::Pod, identity, "", "");

  WorkloadMetadataObject cronjob("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                                 "v1alpha3", "foo-app", "v1", WorkloadType::CronJob, identity, "",
                                 "");

  WorkloadMetadataObject job("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                             "v1alpha3", "", "", WorkloadType::Job, identity, "", "");

  EXPECT_EQ(deploy.serializeAsString(),
            absl::StrCat("type=deployment,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));

  EXPECT_EQ(pod.serializeAsString(),
            absl::StrCat("type=pod,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));

  EXPECT_EQ(cronjob.serializeAsString(),
            absl::StrCat("type=cronjob,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3,",
                         "app=foo-app,version=v1"));

  EXPECT_EQ(job.serializeAsString(),
            absl::StrCat("type=job,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));
}

void checkStructConversion(const Envoy::StreamInfo::FilterState::Object& data) {
  const auto& obj = dynamic_cast<const WorkloadMetadataObject&>(data);
  auto pb = convertWorkloadMetadataToStruct(obj);
  auto obj2 = convertStructToWorkloadMetadata(pb);
  EXPECT_EQ(obj2->serializeAsString(), obj.serializeAsString());
  MessageDifferencer::Equals(*(obj2->serializeAsProto()), *(obj.serializeAsProto()));
  EXPECT_EQ(obj2->hash(), obj.hash());
}

TEST(WorkloadMetadataObjectTest, ConversionWithLabels) {
  constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
  WorkloadMetadataObject deploy("pod-foo-1234", "my-cluster", "default", "foo", "foo-service",
                                "v1alpha3", "", "", WorkloadType::Deployment, identity, "", "");
  deploy.setLabels({{"label1", "value1"}, {"label2", "value2"}});
  auto pb = convertWorkloadMetadataToStruct(deploy);
  auto obj1 = convertStructToWorkloadMetadata(pb, {"label1", "label2"});
  EXPECT_EQ(obj1->getLabels().size(), 2);
  auto obj2 = convertStructToWorkloadMetadata(pb, {"label1"});
  EXPECT_EQ(obj2->getLabels().size(), 1);
  absl::flat_hash_set<std::string> empty;
  auto obj3 = convertStructToWorkloadMetadata(pb, empty);
  EXPECT_EQ(obj3->getLabels().size(), 0);
}

TEST(WorkloadMetadataObjectTest, Conversion) {
  {
    constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
    const auto r = convertBaggageToWorkloadMetadata(
        "k8s.deployment.name=foo,k8s.cluster.name=my-cluster,"
        "k8s.namespace.name=default,service.name=foo-service,service.version=v1alpha3,app.name=foo-"
        "app,app.version=latest",
        identity);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), DeploymentSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    EXPECT_EQ(r->identity(), identity);
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata(
        "k8s.pod.name=foo-pod-435,k8s.cluster.name=my-cluster,k8s.namespace.name="
        "test,k8s.instance.name=foo-instance-435,service.name=foo-service,service.version=v1beta2");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta2");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), PodSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-pod-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "foo-instance-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta2");
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata(
        "k8s.job.name=foo-job-435,k8s.cluster.name=my-cluster,k8s.namespace.name="
        "test,k8s.instance.name=foo-instance-435,service.name=foo-service,service.version=v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), JobSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-job-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "foo-instance-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta4");
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata(
        "k8s.cronjob.name=foo-cronjob,k8s.cluster.name=my-cluster,"
        "k8s.namespace.name=test,service.name=foo-service,service.version=v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), CronJobSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-cronjob");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta4");
    checkStructConversion(*r);
  }

  {
    const auto r =
        convertBaggageToWorkloadMetadata("k8s.deployment.name=foo,k8s.namespace.name=default,"
                                         "service.name=foo-service,service.version=v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), DeploymentSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1alpha3");
    checkStructConversion(*r);
  }

  {
    const auto r =
        convertBaggageToWorkloadMetadata("service.name=foo-service,service.version=v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1alpha3");
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata("app.name=foo-app,app.version=latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata(
        "service.name=foo-service,service.version=v1alpha3,app.name=foo-app,app.version=latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    checkStructConversion(*r);
  }

  {
    const auto r = convertBaggageToWorkloadMetadata("k8s.namespace.name=default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    checkStructConversion(*r);
  }
}

TEST(WorkloadMetadataObjectTest, ConvertFromEmpty) {
  Protobuf::Struct node;
  auto obj = convertStructToWorkloadMetadata(node);
  EXPECT_EQ(obj->serializeAsString(), "");
  checkStructConversion(*obj);
}

TEST(WorkloadMetadataObjectTest, ConvertFromEndpointMetadata) {
  EXPECT_EQ(absl::nullopt, convertEndpointMetadata(""));
  EXPECT_EQ(absl::nullopt, convertEndpointMetadata("a;b"));
  EXPECT_EQ(absl::nullopt, convertEndpointMetadata("a;;;b"));
  EXPECT_EQ(absl::nullopt, convertEndpointMetadata("a;b;c;d"));
  auto obj = convertEndpointMetadata("foo-pod;default;foo-service;v1;my-cluster");
  ASSERT_TRUE(obj.has_value());
  EXPECT_EQ(obj->serializeAsString(), "workload=foo-pod,cluster=my-cluster,"
                                      "namespace=default,service=foo-service,revision=v1");
}

} // namespace Common
} // namespace Istio
} // namespace Envoy
