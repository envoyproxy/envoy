#include "envoy/common/exception.h"

#include "common/network/utility.h"

namespace Network {

TEST(IpWhiteListTest, Errors) {
  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["foo"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpWhiteList wl(*loader); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["foo/bar"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpWhiteList wl(*loader); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["192.168.1.1/33"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpWhiteList wl(*loader); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["192.168.1.1/24"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpWhiteList wl(*loader); }, EnvoyException);
  }
}

TEST(IpWhiteListTest, Normal) {
  std::string json = R"EOF(
  {
    "ip_white_list": [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16"
     ]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  IpWhiteList wl(*loader);

  EXPECT_TRUE(wl.contains("192.168.3.0"));
  EXPECT_TRUE(wl.contains("192.168.3.3"));
  EXPECT_TRUE(wl.contains("192.168.3.255"));
  EXPECT_FALSE(wl.contains("192.168.2.255"));
  EXPECT_FALSE(wl.contains("192.168.4.0"));

  EXPECT_TRUE(wl.contains("50.1.2.3"));
  EXPECT_FALSE(wl.contains("50.1.2.2"));
  EXPECT_FALSE(wl.contains("50.1.2.4"));

  EXPECT_TRUE(wl.contains("10.15.0.0"));
  EXPECT_TRUE(wl.contains("10.15.90.90"));
  EXPECT_TRUE(wl.contains("10.15.255.255"));
  EXPECT_FALSE(wl.contains("10.14.255.255"));
  EXPECT_FALSE(wl.contains("10.16.0.0"));

  EXPECT_FALSE(wl.contains(""));
}

TEST(IpWhiteListTest, MatchAny) {
  std::string json = R"EOF(
  {
    "ip_white_list": [
      "0.0.0.0/0"
     ]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  IpWhiteList wl(*loader);

  EXPECT_TRUE(wl.contains("192.168.3.3"));
  EXPECT_TRUE(wl.contains("192.168.3.0"));
  EXPECT_TRUE(wl.contains("192.168.3.255"));
  EXPECT_TRUE(wl.contains("192.168.0.0"));
  EXPECT_TRUE(wl.contains("192.0.0.0"));
  EXPECT_TRUE(wl.contains("1.1.1.1"));

  EXPECT_FALSE(wl.contains(""));
}

TEST(NetworkUtility, NonNumericResolve) {
  EXPECT_THROW(Utility::resolveTCP("localhost", 80), EnvoyException);
}

TEST(NetworkUtility, Url) {
  EXPECT_EQ("foo", Utility::hostFromUrl("tcp://foo:1234"));
  EXPECT_EQ(1234U, Utility::portFromUrl("tcp://foo:1234"));
  EXPECT_THROW(Utility::hostFromUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::hostFromUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromUrl("tcp://foo:bar"), EnvoyException);
  EXPECT_THROW(Utility::hostFromUrl(""), EnvoyException);
}

TEST(NetworkUtility, GetLocalAddress) {
  std::string addr = Utility::getLocalAddress();
  Utility::resolveTCP(addr, 80);
}

TEST(NetworkUtility, loopbackAddress) {
  {
    std::string address = "127.0.0.1";
    EXPECT_TRUE(Utility::isLoopbackAddress(address.c_str()));
  }
  {
    std::string address = "10.0.0.1";
    EXPECT_FALSE(Utility::isLoopbackAddress(address.c_str()));
  }
}

TEST(NetworkUtility, PortRangeList) {
  {
    std::string port_range_str = "1";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(1, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(2, port_range_list));
  }

  {
    std::string port_range_str = "1024-2048";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(1024, port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(2048, port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(1536, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(1023, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(2049, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(0, port_range_list));
  }

  {
    std::string port_range_str = "1,10-100,1000-10000,65535";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(1, port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(50, port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(5000, port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(65535, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(2, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(200, port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(20000, port_range_list));
  }
}

} // Network
