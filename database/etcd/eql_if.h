#ifndef DATABASE_ETCD_ETCDQL_IF_H_
#define DATABASE_ETCD_ETCDQL_IF_H_

#include <string>
#include <boost/asio/ip/tcp.hpp>
#include <grpc++/grpc++.h>
#include <boost/function.hpp>
#include "proto/rpc.grpc.pb.h"
#include "proto/kv.pb.h"

using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using grpc::ClientAsyncResponseReader;
using grpc::ClientAsyncReaderWriter;

using etcdserverpb::KV;
using etcdserverpb::Watch;
using etcdserverpb::RangeResponse;
using etcdserverpb::PutResponse;
using etcdserverpb::DeleteRangeResponse;
using etcdserverpb::WatchRequest;
using etcdserverpb::WatchResponse;

namespace etcd {
namespace etcdql {

typedef boost::asio::ip::tcp::endpoint Endpoint;

class EtcdResponse;

/**
 * EtcdIf is the etcd client that is used to create and maintain connection
 * to the etcd server.
 * The methods of the client can be used to perform etcd operations.
 * Control node is only interested in the reading data from or watching
 * changes on a specific key or directory in etcd and hence only those
 * operations are implemented here.
 */
class EtcdIf {
public:
    /**
     * Types
     */
    typedef boost::function<void (const EtcdResponse& Resp)> WatchCb;

    /**
     * Constructor that creates an etcd client object.
     * @param etcd_hosts The IP address(es)of the etcd server
     * @param port The port to connect to
     */
    EtcdIf(const std::vector<std::string> &etcd_hosts,
           const int port, bool useSsl);

    virtual ~EtcdIf();

    /**
     * Open a grpc connection to the etcd server.
     */
    virtual bool Connect();

    /**
     * Sends a GET request to the etcd server to get data for key
     * or directory rooted at key.
     * @param key The key or directory to be read
     * @param range_end The key range to fetch
     * @param limit The number of keys to read
     */
    virtual EtcdResponse Get(const std::string &key,
                             const std::string &range_end,
                             int limit);

    /**
     * ONLY FOR TEST PURPOSES.
     * Sends a SET request to the etcd server to create or update
     * a key-value pair.
     * @param key The key to be created/updated
     * @param value The value corresponding to the key.
     */
    virtual void Set(const std::string &key,
                     const std::string &value);

    /**
     * ONLY FOR TEST PURPOSES.
     * Sends a DELETE request to the etcd server to delete a
     * key or set of keys
     * @param key The key or directory to be deleted
     * @param range_end The key range to fetch
     */
    virtual void Delete(const std::string &key,
                        const std::string &range_end);

    /**
     * Watches for changes to a key or directory rooted at key.
     * And invokes callback when an update is received.
     * @param key The key or directory to be watched
     * @param callback The callback to be invoked when there is an update
     */
    virtual void Watch(const std::string &key,
                       WatchCb cb);

    /**
     * Stop the watch request if scheduled
     */
    virtual void StopWatch();

    int port() const { return port_; }
    std::vector<Endpoint> endpoints() const { return endpoints_; }
    std::vector<std::string> hosts() const { return hosts_; }

private:
    /**
     * Data
     */
    std::vector<Endpoint> endpoints_;
    std::vector<std::string> hosts_;
    int port_;
    bool useSsl_;
    std::unique_ptr<KV::Stub> kv_stub_;
    std::unique_ptr<Watch::Stub> watch_stub_;

    /**
     * For ETCD get/watch request
     */
    struct EtcdAsyncCall {
        Status status_;
        ClientContext ctx_;
        CompletionQueue cq_;
    };

    /**
     * For get operation
     */
    struct EtcdAsyncGetCall : public EtcdAsyncCall {
        std::unique_ptr<ClientAsyncResponseReader<RangeResponse>> get_reader_;
        int gtag_;
        RangeResponse get_resp_;
        EtcdResponse ParseGetResponse();
    };
    std::unique_ptr<EtcdAsyncGetCall> get_call_;

    /**
     * TEST ONLY
     * For set operation
     */
    struct EtcdAsyncSetCall : public EtcdAsyncCall {
        std::unique_ptr<ClientAsyncResponseReader<PutResponse>> set_reader_;
        PutResponse set_resp_;
    };
    std::unique_ptr<EtcdAsyncSetCall> set_call_;

    /**
     * TEST ONLY
     * For delete operation
     */
    struct EtcdAsyncDeleteCall : public EtcdAsyncCall {
        std::unique_ptr<ClientAsyncResponseReader<DeleteRangeResponse>> delete_reader_;
        DeleteRangeResponse delete_resp_;
    };
    std::unique_ptr<EtcdAsyncDeleteCall> delete_call_;

    /**
     * For watch operation
     */
    struct EtcdAsyncWatchCall : public EtcdAsyncCall {
        std::unique_ptr<ClientAsyncReaderWriter<WatchRequest,WatchResponse>> watch_reader_;
        bool watch_active_;
        int wtag;
        WatchResponse watch_resp_;
        void WaitForWatchResponse(WatchCb cb);
    };
    std::unique_ptr<EtcdAsyncWatchCall> watch_call_;
};

typedef enum {
    CREATE=0,
    UPDATE,
    DELETE,
    INVALID
} WatchAction;

/**
 * Wrapper to store the response received from ETCD
 * get or watch operations.
 */
class EtcdResponse {
public:
    typedef std::multimap<std::string, std::string> kv_map;

    EtcdResponse()
        : ec_(0) {};

    int err_code() const { return ec_; }
    void set_err_code(int code) {
        ec_ = code;
    }

    const std::string& err_msg() const { return msg_; }
    void set_err_msg(std::string msg) {
       msg_ = msg;
    }

    int revision() const { return revision_; }
    void set_revision (int revision) {
        revision_ = revision;
    }

    WatchAction action() const { return action_; }
    void set_action(WatchAction action) {
       action_ = action;
    }

    const std::string& key() const { return key_; }
    void set_key(std::string key) {
       key_ = key;
    }

    const std::string& value() const { return val_; }
    void set_val(std::string val) {
       val_ = val;
    }

    const std::string& prev_key() const { return prev_key_; }
    void set_prev_key(std::string prev_key) {
       prev_key_ = prev_key;
    }

    const std::string& prev_value() const { return prev_val_; }
    void set_prev_val(std::string prev_val) {
       prev_val_ = prev_val;
    }

    const kv_map& kvmap() const { return kv_map_; }
    void set_kv_map(kv_map kvs) {
       kv_map_ = kvs;
    }

private:
    int ec_;
    std::string msg_;
    WatchAction action_;
    std::string key_;
    std::string val_;
    std::string prev_key_;
    std::string prev_val_;
    int revision_;
    kv_map kv_map_;
};

} //namespace etcdql
} //namespace etcd
#endif
