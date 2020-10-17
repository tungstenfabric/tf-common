/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef config_k8s_client_h
#define config_k8s_client_h

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/shared_ptr.hpp>

#include "database/k8s/k8s_client.h"

#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/queue_task.h"
#include "base/timer.h"

#include "config_db_client.h"
#include "config_json_parser_base.h"
#include "json_adapter_data.h"

using namespace std;
using contrail_rapidjson::Document;
using contrail_rapidjson::Value;
using k8s::client::K8sClient;
using k8s::client::DomPtr;

class EventManager;
class ConfigClientManager;
struct ConfigDBConnInfo;
class TaskTrigger;
class ConfigK8sClient;
class ConfigDBUUIDCacheEntry;

class ConfigK8sPartition
{
public:
    ConfigK8sPartition(ConfigK8sClient *client, size_t idx);
    virtual ~ConfigK8sPartition();

    typedef std::unique_ptr<WorkQueue<ObjectProcessReq *>>
        UUIDProcessRequestQPtr;

    class UUIDCacheEntry : public ObjectCacheEntry
    {
    public:
        UUIDCacheEntry(ConfigK8sPartition *parent,
                       const string &value_str,
                       uint64_t last_read_tstamp)
            : ObjectCacheEntry(last_read_tstamp),
              json_str_(value_str),
              parent_(parent)
        {
        }

        ~UUIDCacheEntry();

        const string &GetJsonString() const { return json_str_; }
        void SetJsonString(const string &value_str)
        {
            json_str_ = value_str;
        }

        void SetListOrMapPropEmpty(const string &prop, bool empty)
        {
            prop_empty_map_.insert(make_pair(prop.c_str(), empty));
        }
        bool ListOrMapPropEmpty(const string &prop) const;

    private:
        friend class ConfigK8sJsonPartitionTest;
        typedef map<string, bool> PropEmptyMap;
        PropEmptyMap prop_empty_map_;
        string json_str_;
        ConfigK8sPartition *parent_;
    };

    typedef boost::ptr_map<string, UUIDCacheEntry> UUIDCacheMap;

    // Get the cache entry if it exists, or return NULL.
    UUIDCacheEntry *GetUUIDCacheEntry(const string &uuid);
    // Get the cache entry if it exists or create/set it.
    // is_new flag is set to true if it is new.
    UUIDCacheEntry *GetUUIDCacheEntry(const string &uuid,
                                      const string &value_str,
                                      bool &is_new);
    void DeleteUUIDCacheEntry(const string &uuid)
    {
        uuid_cache_map_.erase(uuid);
    }

    void FillUUIDToObjCacheInfo(const string &uuid,
                                UUIDCacheMap::const_iterator uuid_iter,
                                ConfigDBUUIDCacheEntry *entry) const;
    bool UUIDToObjCacheShow(
        const string &search_string, const string &last_uuid,
        uint32_t num_entries,
        vector<ConfigDBUUIDCacheEntry> *entries) const;

    int GetInstanceId() const { return worker_id_; }

    void Enqueue(ObjectProcessReq *req);
    bool IsListOrMapPropEmpty(const string &uuid_key,
                              const string &lookup_key);
    virtual bool IsTaskTriggered() const;

protected:
    ConfigK8sClient *client()
    {
        return config_client_;
    }

private:
    struct UUIDProcessRequestType
    {
        UUIDProcessRequestType(const string &in_oper,
                               const string &in_uuid,
                               const string &in_value_str)
            : oper(in_oper), uuid(in_uuid), value_str(in_value_str)
        {
        }
        string oper;
        string uuid;
        string value_str;
    };

    typedef map<string, boost::shared_ptr<UUIDProcessRequestType>> UUIDProcessRequestMap;

    bool ObjectProcessReqHandler(ObjectProcessReq *req);
    void AddUUIDToProcessRequestMap(const string &oper,
                                    const string &uuid,
                                    const string &value_str);
    bool ConfigReader();
    void ProcessUUIDUpdate(const string &uuid,
                           const string &value_str);
    void ProcessUUIDDelete(const string &uuid_key);
    virtual bool GenerateAndPushJson(
        const string &uuid_key,
        Document &doc,
        bool add_change,
        UUIDCacheEntry *cache);
    void RemoveObjReqEntry(string &uuid);

    boost::shared_ptr<TaskTrigger> config_reader_;

    // Pointer to incoming work for this partition (thread).
    // Work include type (CREATE/UPDATE/DELETE), UUID and value (json).
    UUIDProcessRequestQPtr obj_process_request_queue_;
    // Map of UUID to process requests.
    UUIDProcessRequestMap uuid_process_request_map_;

    // Map of UUID to JSON data.
    UUIDCacheMap uuid_cache_map_;

    ConfigK8sClient *config_client_;
    int worker_id_;
};

/*
 * This class has the functionality to interact with the cassandra servers that
 * store the user configuration.
 */
class ConfigK8sClient : public ConfigDbClient
{
public:
    typedef vector<ConfigK8sPartition *> PartitionList;

    ConfigK8sClient(ConfigClientManager *mgr, EventManager *evm,
                    const ConfigClientOptions &options,
                    int num_workers);
    virtual ~ConfigK8sClient();

    // Configure the map of K8s names to Cass
    virtual void K8sToCassNameConversionInit();

    // Called by InitConfigClient() to initialize bulk synchronization.
    virtual void InitDatabase();
    void BulkSyncDone();

    typedef enum {
        INVALID,
        ADDED,
        MODIFIED,
        DELETED
    } WatchEventType;
    void EnqueueUUIDRequest(string oper, string uuid, string value);

    ConfigK8sPartition *GetPartition(const string &uuid);
    const ConfigK8sPartition *GetPartition(const string &uuid) const;
    const ConfigK8sPartition *GetPartition(int worker_id) const;

    // Start K8s watch for config updates
    // Invoked by ConfigClientManager
    void StartWatcher();

    // UUID Cache
    virtual bool UUIDToObjCacheShow(
        const string &search_string, int inst_num,
        const string &last_uuid, uint32_t num_entries,
        vector<ConfigDBUUIDCacheEntry> *entries) const;

    virtual bool IsListOrMapPropEmpty(const string &uuid_key,
                                      const string &lookup_key);

    bool IsTaskTriggered() const;
    virtual void ProcessResponse(std::string type, DomPtr dom_ptr);

    // For testing
    static void set_watch_disable(bool disable)
    {
        disable_watch_ = disable;
    }

    // Persist a value to a string.
    // This is not terribly efficient since it constructs the return value.
    static string JsonToString(const Value& json_value);

    // Convert a UUID into a pair of longs in big-endian format.
    // Sets longs[0] are the most-significant bytes,
    // and longs[1] to the least-significant bytes.
    static void UUIDToLongLongs(
        const string& uuid, unsigned long long longs[]);

    // Convert a Cassandra type name into a Kubernetes type name
    string CassTypeToK8sKind(const std::string& cass_type);

    // Convert an fq_name value to a string.
    // Optionally truncate values from the end.
    static string FqNameToString(const Value& fq_name_array, size_t truncate);

    // Convert a reference name and fq_name to a string
    static string FqNameToParentRefString(const Value& fq_name_array) {
        return FqNameToString(fq_name_array, 1);
    }

    // Convert a TypeName or fieldName to type_name or field_name
    string K8sNameConvert(
        const char* name, unsigned length);

    // Convert a K8s JSON into Cassandra JSON
    void K8sJsonConvert(
        const Document& k8s_dom, Document& cass_dom);

    // Adds a K8s ref(s) to a Cassandra dom
    void K8sJsonAddRefs(
        Value::ConstMemberIterator& refs, Value::ConstMemberIterator& fq_name, Document& cass_dom, map<string, Value>& ref_map);

    // Populate a Cassandra ref property object with data from Kubernetes.
    Value K8sJsonCreateRef(const Value* ref_info, Document& cass_dom);

    // Adds a member (recursively) from K8s dom into a Cassandra dom.
    void K8sJsonMemberConvert(
        Value::ConstMemberIterator& member,
        Value& dom, Document::AllocatorType& alloc);

    // Adds a member (recursively) from K8s dom into a Cassandra dom.
    void K8sJsonValueConvert(
        const Value& value,
        Value& converted, Document::AllocatorType& alloc);

    static const std::string api_group_;
    static const std::string api_version_;

protected:
    virtual bool BulkDataSync();
    void EnqueueDBSyncRequest(DomPtr dom_ptr);

    virtual int HashUUID(const std::string &uuid_str) const;
    int num_workers() const { return num_workers_; }
    PartitionList &partitions() { return partitions_; }
    virtual void PostShutdown();

private:
    friend class ConfigK8sPartition;

    // A Job for watching changes to config stored in K8s
    class K8sWatcher;

    bool InitRetry();

    // Base URL for K8s API
    static const std::string k8s_api_url_base_;

    // BulkDataSync of all object types from K8s
    bool UUIDReader();

    // Set and log connection status
    void HandleK8sConnectionStatus(bool success,
                                   bool force_update = false);

    // For testing
    static bool disable_watch_;

    boost::scoped_ptr<K8sClient> k8s_client_;
    const int num_workers_;
    PartitionList partitions_;
    boost::scoped_ptr<TaskTrigger> uuid_reader_;
    tbb::atomic<long> bulk_sync_status_;

    // "special case" K8s to cassandra JSON name mapping.
    map<string, string> k8s_to_cass_name_conversion_;
    map<string, string> cass_to_k8s_name_conversion_;
};

#endif // config_k8s_client_h
