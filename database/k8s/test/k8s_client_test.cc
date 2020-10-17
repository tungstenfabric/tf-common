//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#include <testing/gunit.h>

#include <thread>
#include <base/logging.h>
#include <base/queue_task.h>
#include <base/task.h>
#include <boost/bind.hpp>
#include <iostream>
#include <database/k8s/k8s_client.h>
#include <restclient-cpp/restclient.h>
#include <restclient-cpp/connection.h>

using namespace std;
using k8s::client::K8sClient;

class K8sClientTest : public ::testing::Test {
    protected:
        k8s::client::K8sUrl url_;
        RestClient::Connection cx_;
        // k8s::client::K8sUrl k8sUrl_;
        // k8s::client::WatcherPtr watcherPtr_;

        K8sClientTest() : url_("https://127.0.0.1:32770/apis", "core.contrail.juniper.net", "v1alpha1"), cx_(url_.serverUrl()) {
            // k8sUrl_ = k8s::client::K8sUrl("http://127.0.0.1:8001", "core.contrail.juniper.net", "v1alpha1");
        }
        virtual ~K8sClientTest(){
        }
        virtual void SetUp() {
            // watcherPtr_.reset(new k8s::client::K8sWatcher(k8sUrl_, "globalsystemconfig", ));
        }
        virtual void TearDown() {
            // watcherPtr_.reset();
        }
};


TEST_F(K8sClientTest, TestGet) {
    cx_.SetCertPath("/root/projects/contrail-config-ng/contrail/hack/kind-kind.pem");
    cx_.SetCertType("PEM");
    string namePath = url_.namePath("globalsystemconfigs?watch=1&resourceVersion=");
    // RestClient::Response r = cx_.get(namePath);
    // EXPECT_EQ(200, r.code);
}

// TEST_F(K8sClientTest, UpdateKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Set("/contrail/vn1", "updated vn1");

//     EtcdResponse resp;
//     EtcdResponse::kv_map kvs;

//     resp = etcd.Get("/contrail/vn1", "", 4);
//     kvs = resp.kvmap();

//     EXPECT_EQ(resp.err_code(), 0);
//     EXPECT_EQ(kvs.size(), 1);
//     EXPECT_EQ(kvs.find("/contrail/vn1")->first, "/contrail/vn1");
//     EXPECT_EQ(kvs.find("/contrail/vn1")->second, "updated vn1");
// }

// TEST_F(K8sClientTest, ReadKeys)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Delete("/", "\\0");
//     etcd.Set("/contrail/vn1", "1");
//     etcd.Set("/contrail/vn2", "2");
//     etcd.Set("/contrail/vn3", "3");
//     etcd.Set("/contrail/vn4", "4");
//     etcd.Set("/contrail/vn5", "5");
//     etcd.Set("/contrail/vn6", "6");
//     etcd.Set("/contrail/vn6", "7");

//     EtcdResponse resp;
//     EtcdResponse::kv_map kvs;
//     string str;

//     // Find keys with prefix "/" 4 at a time
//     resp = etcd.Get("/", "\\0", 4);
//     kvs = resp.kvmap();

//     EXPECT_EQ(kvs.size(), 4);
//     EXPECT_EQ(resp.err_code(), 0);

//     int i = 1;
//     while (kvs.size() == 4) {
//         for (multimap<string, string>::const_iterator iter = kvs.begin();
//             iter != kvs.end(); ++iter, ++i) {
//             str = iter->first;
//             EXPECT_EQ(iter->second, to_string(i));
//         }
//         // Get the next key
//         str += "00";
//         resp =  etcd.Get(str, "\\0", 4);
//         kvs = resp.kvmap();
//     }
// }

// TEST_F(K8sClientTest, ReadLimit)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Delete("/", "\\0");
//     etcd.Set("/contrail/vn1", "1");
//     etcd.Set("/contrail/vn2", "2");
//     etcd.Set("/contrail/vn3", "3");
//     etcd.Set("/contrail/vn4", "4");
//     etcd.Set("/contrail/vn5", "5");

//     EtcdResponse resp;
//     EtcdResponse::kv_map kvs;

//     // Find keys with prefix "/" 4 at a time
//     resp = etcd.Get("/", "\\0", 4);
//     kvs = resp.kvmap();

//     EXPECT_EQ(kvs.size(), 4);
//     EXPECT_EQ(resp.err_code(), 0);

//     resp = etcd.Get("/", "\\0", 3);
//     kvs = resp.kvmap();

//     EXPECT_EQ(kvs.size(), 3);
//     EXPECT_EQ(resp.err_code(), 0);

//     resp = etcd.Get("/", "\\0", 0);
//     kvs = resp.kvmap();

//     EXPECT_EQ(kvs.size(), 5);
//     EXPECT_EQ(resp.err_code(), 0);
// }

// TEST_F(K8sClientTest, ReadUnknownKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Delete("/", "\\0");
//     etcd.Set("/contrail/vn1", "1");
//     etcd.Set("/contrail/vn2", "2");

//     k8s::client::EtcdResponse resp;

//     // Key not found
//     resp = etcd.Get("abc", "\\0", 4);

//     string str = "Prefix/Key not found";

//     EXPECT_EQ(100, resp.err_code());
//     EXPECT_EQ(str, resp.err_msg());
// }

// TEST_F(K8sClientTest, ReadOneKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Delete("/", "\\0");
//     etcd.Set("/contrail/vn1", "1");
//     etcd.Set("/contrail/vn2", "2");

//     k8s::client::EtcdResponse resp;
//     k8s::client::EtcdResponse::kv_map kvs;

//     // Read a single key
//     resp = etcd.Get("/contrail/vn2", "", 1);

//     kvs = resp.kvmap();

//     EXPECT_EQ(resp.err_code(), 0);
//     EXPECT_EQ(kvs.size(), 1);
//     EXPECT_EQ(kvs.find("/contrail/vn2")->first, "/contrail/vn2");
//     EXPECT_EQ(kvs.find("/contrail/vn2")->second, "2");
// }

// TEST_F(K8sClientTest, DeleteOneKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Set("/contrail/vn1", "1");
//     etcd.Delete("/contrail/vn1", "");

//     k8s::client::EtcdResponse resp;

//     resp = etcd.Get("/contrail/vn1", "", 4);

//     string str = "Prefix/Key not found";

//     EXPECT_EQ(100, resp.err_code());
//     EXPECT_EQ(str, resp.err_msg());
// }

// TEST_F(K8sClientTest, DeleteAllKeys)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     etcd.Delete("/", "\\0");

//     k8s::client::EtcdResponse resp;
//     k8s::client::EtcdResponse::kv_map kvs;

//     resp = etcd.Get("/", "\\0", 10);

//     string str = "Prefix/Key not found";

//     EXPECT_EQ(100, resp.err_code());
//     EXPECT_EQ(str, resp.err_msg());
//     EXPECT_EQ(kvs.size(), 0);
// }

// void WatchForSetChanges(EtcdResponse resp) {
//     EXPECT_EQ("1", to_string(resp.action()));
//     EXPECT_EQ(resp.key(), "/contrail/vn1/");
//     EXPECT_EQ(resp.value(), "1");
//     EXPECT_EQ(resp.err_code(), 0);
// }

// void WatchSetReq(k8s::client::K8sClient *etcd) {
//     etcd->Watch("/", &WatchForSetChanges);
// }

// void StopWatch(k8s::client::K8sClient *etcd) {
//     etcd->StopWatch();
// }

// TEST_F(K8sClientTest, WatchSetKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     std::thread th1 = thread(&WatchSetReq, &etcd);
//     etcd.Set("/contrail/vn1/", "1");

//     thread th2 = thread(&StopWatch, &etcd);
//     th1.join();
//     th2.join();
// }

// void WatchForDelChanges(EtcdResponse resp) {
//     EXPECT_EQ("2", to_string(resp.action()));

//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     EtcdResponse resp1;

//     resp1 = etcd.Get("/contrail/vn1/", "\\0", 4);

//     string str = "Prefix/Key not found";

//     EXPECT_EQ(100, resp1.err_code());
//     EXPECT_EQ(str, resp1.err_msg());
// }

// void WatchDelReq(k8s::client::K8sClient *etcd) {
//     etcd->Watch("/", &WatchForDelChanges);
// }

// TEST_F(K8sClientTest, WatchDeleteKey)
// {
//     vector<string> hosts = {"127.0.0.1"};
//     K8sClient etcd(hosts, 2379, false);
//     etcd.Connect();
//     std::thread th1 = thread(&WatchDelReq, &etcd);
//     etcd.Delete("/contrail/vn1/", "");

//     thread th2 = thread(&StopWatch, &etcd);
//     th1.join();
//     th2.join();
// }

TEST_F(K8sClientTest, Nothing)
{
    int one = 1;
    EXPECT_EQ(1, one);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
