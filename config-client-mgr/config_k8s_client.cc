/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/request_pipeline.h>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "config_client_log.h"
#include "config_client_log_types.h"
#include "config_client_show_types.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <utility>

#include "base/string_util.h"
#include "base/connection_info.h"
#include "base/logging.h"
#include "base/regex.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "config_cass2json_adapter.h"
#include "io/event_manager.h"
#include "config_factory.h"
#include "config_client_log.h"
#include "config_client_log_types.h"
#include "config_client_show_types.h"
#include "sandesh/common/vns_constants.h"

#include "config-client-mgr/config_k8s_client.h"

using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using namespace std;
using contrail_rapidjson::Document;
using contrail_rapidjson::SizeType;
using contrail_rapidjson::StringBuffer;
using contrail_rapidjson::Value;
using contrail_rapidjson::Writer;
using k8s::client::K8sClient;
using k8s::client::K8sUrl;

bool ConfigK8sClient::disable_watch_;

const std::string ConfigK8sClient::api_group_ = "core.contrail.juniper.net";
const std::string ConfigK8sClient::api_version_ = "v1alpha1";

/**
  * K8S Watcher class to enable watching for any changes
  * to config.
  * Invokes k8s::Watch() which watches the K8S server for
  * any changes and invokes provided callback when a change
  * is detected.
  */
class ConfigK8sClient::K8sWatcher : public Task
{
public:
    K8sWatcher(ConfigK8sClient *k8s_client)
        : Task(TaskScheduler::GetInstance()->GetTaskId("k8s::K8sWatcher")),
          k8s_client_(k8s_client)
    {
    }

    virtual bool Run();

    virtual void OnTaskCancel();

    ConfigK8sClient *client() const
    {
        return k8s_client_;
    }
    string Description() const
    {
        return "ConfigK8sClient::K8sWatcher";
    }

private:
    ConfigK8sClient *k8s_client_;
};

ConfigK8sClient::ConfigK8sClient(ConfigClientManager *mgr,
                                 EventManager *evm,
                                 const ConfigClientOptions &options,
                                 int num_workers)
    : ConfigDbClient(mgr, evm, options),
      num_workers_(num_workers)
{
    // Initialize map of K8s property names to Cassandra JSON
    K8sToCassNameConversionInit();

    std::vector<K8sUrl> k8sUrls;
    for(size_t i = 0; i < config_db_ips().size(); ++i)
    {
        std::string server = config_db_ips().empty() ? "127.0.0.1" : config_db_ips()[i];
        size_t port = config_db_ports().empty() ? 8001 : config_db_ports()[i];
        ostringstream service_url;
        service_url << (options.config_db_use_ssl ? "https" : "http") << "://" << server << ':' << port << "/apis";
        K8sUrl k8s_url(service_url.str(), api_group_, api_version_);
        k8sUrls.push_back(k8s_url);
    }

    k8s_client_.reset(ConfigFactory::Create<K8sClient>(
        k8sUrls,
        options.config_db_ca_certs,
        0,
        GetNumReadRequestToBunch()));
    InitConnectionInfo();
    bulk_sync_status_ = 0;

    for (int i = 0; i < num_workers_; i++)
    {
        partitions_.push_back(
            ConfigFactory::Create<ConfigK8sPartition>(this, i));
    }

    uuid_reader_.reset(new TaskTrigger(boost::bind(&ConfigK8sClient::UUIDReader, this),
                                       TaskScheduler::GetInstance()->GetTaskId("config_client::DBReader"),
                                       0));
}

ConfigK8sClient::~ConfigK8sClient()
{
    STLDeleteValues(&partitions_);
}

// Initialize map of K8s property names to Cassandra JSON
void ConfigK8sClient::K8sToCassNameConversionInit()
{
    // Find the map file
    auto k8s_to_cass_file_name_c_str = std::getenv("CONFIG_K8S_MAP");
    std::string k8s_to_cass_file_name;
    if (k8s_to_cass_file_name_c_str != NULL)
    {
        k8s_to_cass_file_name = k8s_to_cass_file_name_c_str;
    }
    else
    {
        k8s_to_cass_file_name = "config_k8s_map.txt";
    }

    // Try to read from the override file first
    std::ifstream k8s_to_cass_file(k8s_to_cass_file_name.c_str());
    if (k8s_to_cass_file.is_open())
    {
        std::string line;
        while (std::getline(k8s_to_cass_file, line))
        {
            // File is simple "key=value" w/o spaces
            // Skip lines without '='
            auto delim = line.find('=');
            if (delim == std::string::npos)
            {
                continue;
            }
            auto k8s_name = line.substr(0, delim);
            auto cass_name = line.substr(delim + 1, line.size() - 1 - delim);
            k8s_to_cass_name_conversion_[k8s_name] = cass_name;
        }
    }
    else
    {
        // Known defaults if override file is missing
        k8s_to_cass_name_conversion_["attributes"] = "attr";
        k8s_to_cass_name_conversion_["NetworkIPAM"] = "network_ipam";
        k8s_to_cass_name_conversion_["InstanceIP"] = "instance_ip";
        k8s_to_cass_name_conversion_["BGPRouter"] = "bgp_router";
        k8s_to_cass_name_conversion_["fabricSNAT"] = "fabric_snat";
        k8s_to_cass_name_conversion_["routingInstanceFabricSNAT"] = "routing_instance_fabric_snat";
    }

    // Automatically populate a reverse map as well
    for(auto key_value = k8s_to_cass_name_conversion_.begin();
        key_value != k8s_to_cass_name_conversion_.end();
        ++key_value)
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S SM: Mapped K8s keyword " + key_value->first + " to " + key_value->second);
        cass_to_k8s_name_conversion_[key_value->second] = key_value->first;
    }
}

void ConfigK8sClient::StartWatcher()
{
    if (disable_watch_)
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: StartWatcher: K8S watch disabled");
        return;
    }

    /**
      * If reinit is triggerred, Don't start the K8S watcher.
      */
    if (mgr()->is_reinit_triggered())
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: StartWatcher: re init triggered,"
            " don't enqueue K8S Watcher Task.");
        return;
    }

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    Task *task = new K8sWatcher(this);
    scheduler->Enqueue(task);
}

bool ConfigK8sClient::K8sWatcher::Run()
{
    /**
      * If reinit is triggerred, don't wait for end of config
      * trigger. Return from here to process reinit.
      */
    if (k8s_client_->mgr()->is_reinit_triggered())
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: Run: re init triggered,"
            " don't wait for end of config");
        return true;
    }

    /**
      * Invoke k8s client library to watch for changes.
      */
    client()->k8s_client_->StartWatchAll(
        boost::bind(&ConfigK8sClient::ProcessResponse,
                    k8s_client_, _1, _2));

    return true;
}

void ConfigK8sClient::K8sWatcher::OnTaskCancel()
{
    client()->k8s_client_->StopWatchAll();
}

void ConfigK8sClient::ProcessResponse(std::string type, DomPtr dom_ptr)
{
    /**
      * If reinit is triggerred, don't consume the message.
      * Also, stop k8s watch.
      */
    if (mgr()->is_reinit_triggered())
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: ProcessResponse: re init triggered,"
            " stop watching");
        k8s_client_->StopWatchAll();
        return;
    }

    /**
      * To start consuming the message, we should have finished
      * bulk sync in case we started it.
      */
    mgr()->WaitForEndOfConfig();

    /**
      * Update FQName cache.
      */
    Value::ConstMemberIterator metadata = dom_ptr->FindMember("metadata");
    if (metadata == metadata->value.MemberEnd())
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: ProcessResponse: metadata missing: " +
                JsonToString(*dom_ptr));
        return;
    }

    Value::ConstMemberIterator uid = metadata->value.FindMember("uid");
    if (uid == metadata->value.MemberEnd())
    {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S Watcher SM: ProcessResponse: uid missing: " +
                JsonToString(*dom_ptr));
        return;
    }

    this->EnqueueUUIDRequest(
        type,
        uid->value.GetString(),
        ConfigK8sClient::JsonToString(*dom_ptr));
}

void ConfigK8sClient::InitDatabase()
{
    HandleK8sConnectionStatus(false, true);
    while (true)
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, "K8S SM: Db Init");
        if (k8s_client_->Init() != EXIT_SUCCESS)
        {
            CONFIG_CLIENT_DEBUG(ConfigK8sClientInitErrorMessage,
                                "Database initialization failed");
            if (!InitRetry()) return;
            continue;
        }
        break;
    }
    HandleK8sConnectionStatus(true);
    BulkDataSync();
}

void ConfigK8sClient::HandleK8sConnectionStatus(bool success,
                                                bool force_update)
{
    UpdateConnectionInfo(success, force_update);

    if (success)
    {
        // Update connection info
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "K8S",
            process::ConnectionStatus::UP,
            k8s_client_->endpoints(), "Established K8S connection");
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: Established K8S connection");
    }
    else
    {
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "K8S",
            process::ConnectionStatus::DOWN,
            k8s_client_->endpoints(), "Lost K8S connection");
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: Lost K8S connection");
    }
}

string ConfigK8sClient::JsonToString(const Value& json_value)
{
    StringBuffer string_buffer;
    Writer<StringBuffer> writer(string_buffer);
    json_value.Accept(writer);
    return string_buffer.GetString();
}

// Convert a UUID into a pair of longs in big-endian format.
// Sets longs[0] are the most-significant bytes,
// and longs[1] to the least-significant bytes.
void ConfigK8sClient::UUIDToLongLongs(
    const string& uuid, unsigned long long longs[])
{
    // Use a union to convert byte array to words
    union {
        boost::uuids::uuid uuid;
        __uint128_t data;
    } uuid_union;

    // convert string UUID into binary UUID
    boost::uuids::string_generator string_gen;
    uuid_union.uuid = string_gen(uuid);

    // get the least and most significant bytes
    unsigned long long uuid_first_longlong = uuid_union.data >> 64;
    unsigned long long uuid_second_longlong =
        uuid_union.data & (((__uint128_t)1 << 64) - (__uint128_t)1);

    // convert from big-endian to hardware byte order (usually little-endian)
    unsigned long long uuid_first_longlong_h = be64toh(uuid_first_longlong);
    unsigned long long uuid_second_longlong_h = be64toh(uuid_second_longlong);

    // if we converted the byte order, swap least and most significant bytes
    if (uuid_first_longlong_h == uuid_first_longlong)
    {
        longs[0] = uuid_first_longlong_h;
        longs[1] = uuid_second_longlong_h;
    }
    else
    {
        longs[1] = uuid_first_longlong_h;
        longs[0] = uuid_second_longlong_h;
    }
}

// Convert a cassandra type (lowercase separted by underscored) to
// name to K8s format (CamelCase).
string ConfigK8sClient::CassTypeToK8sKind(const std::string& cass_type)
{
    // First, look for special-cases
    auto override = cass_to_k8s_name_conversion_.find(cass_type);
    if (override != cass_to_k8s_name_conversion_.end())
    {
        return override->second;
    }

    // Otherwise, convert name algrithmically
    string ret;
    bool last_char_underscore = false;
    for (size_t i = 0; i < cass_type.length(); ++i)
    {
        char c = cass_type[i];
        if (i == 0 || last_char_underscore)
        {
            ret += toupper(c);
            last_char_underscore = false;
        }
        else if (c == '_' || c == '-')
        {
            // skip dashes and underscores
            last_char_underscore = true;
        }
        else
        {
            ret += c;
            last_char_underscore = false;
        }
    }
    return ret;
}

string ConfigK8sClient::FqNameToString(const Value& fq_name_array, size_t truncate = 0)
{
    string fq_name_str;
    for (auto name_itr = fq_name_array.Begin();
        (name_itr + truncate) != fq_name_array.End();
        ++name_itr)
    {
        fq_name_str += name_itr->GetString();
        fq_name_str += ":";
    }
    fq_name_str.erase(fq_name_str.end() - 1);

    return fq_name_str;
}

// Convert a TypeName or fieldName to name or field_name
string ConfigK8sClient::K8sNameConvert(
    const char* name, unsigned length)
{
    // First, look for hard-coded mapping
    auto conversion = k8s_to_cass_name_conversion_.find(name);
    if (conversion != k8s_to_cass_name_conversion_.end())
    {
        return conversion->second;
    }

    // Convert the name algorithmically
    string ret;
    for (size_t i = 0; i < length; ++i)
    {
        char c = name[i];
        char lc = tolower(c);
        if (c != lc && i > 0) {
            ret +='_';
        }
        ret += lc;
    }
    return ret;
}

void ConfigK8sClient::K8sJsonMemberConvert(
    Value::ConstMemberIterator& member,
    Value& object,
    Document::AllocatorType& alloc)
{
    // convert the name
    string member_name_string =
        ConfigK8sClient::K8sNameConvert(
            member->name.GetString(), member->name.GetStringLength());
    if (object.FindMember(member_name_string.c_str()) != object.MemberEnd())
    {
        // already set (from status, most likely) -- ignore member
        return;
    }

    Value new_member_name;
    new_member_name.SetString(
        member_name_string.c_str(), member_name_string.length(), alloc);

    // create the new value
    Value converted;
    K8sJsonValueConvert(member->value, converted, alloc);
    object.AddMember(new_member_name, converted, alloc);
}

void ConfigK8sClient::K8sJsonValueConvert(
    const Value& value,
    Value& converted,
    Document::AllocatorType& alloc)
{
    if(value.IsObject())
    {
        // If this is an object, recurse
        converted.SetObject();
        for (auto obj_member =
            value.MemberBegin();
            obj_member != value.MemberEnd();
            ++obj_member)
        {
            // recurse to visit other objects
            ConfigK8sClient::K8sJsonMemberConvert(
                obj_member, converted, alloc);
        }
    }
    else if (value.IsArray())
    {
        // Create array and recurse.
        converted.SetArray();
        for (auto array_elem = value.Begin();
            array_elem != value.End();
            ++array_elem)
        {
            Value array_elem_value;
            // recurse to visit other objects
            ConfigK8sClient::K8sJsonValueConvert(
                *array_elem, array_elem_value, alloc);
            converted.PushBack(array_elem_value, alloc);
        }
    }
    else
    {
        converted.CopyFrom(value, alloc);
    }
}

// Populate a Cassandra ref property object with data from Kubernetes.
Value ConfigK8sClient::K8sJsonCreateRef(const Value* ref_info, Document& cass_dom)
{
    Value ref_array_val;
    ref_array_val.SetObject();

    auto ref_uid = ref_info->FindMember("uid");
    if (ref_uid != ref_info->MemberEnd())
    {
        Value uuid;
        uuid.CopyFrom(ref_uid->value, cass_dom.GetAllocator());
        ref_array_val.AddMember("uuid", uuid, cass_dom.GetAllocator());
    }

    Value::ConstMemberIterator ref_attributes =
        ref_info->FindMember("attributes");
    if (ref_attributes != ref_info->MemberEnd())
    {
        K8sJsonMemberConvert(
            ref_attributes, ref_array_val, cass_dom.GetAllocator());
    }

    auto fq_name = ref_info->FindMember("fqName");
    if (fq_name != ref_info->MemberEnd())
    {
        string fq_name_str = FqNameToString(fq_name->value);
        Value to;
        to.SetString(fq_name_str.c_str(), cass_dom.GetAllocator());
        ref_array_val.AddMember("to", to, cass_dom.GetAllocator());
    }
    
    return ref_array_val;
}

void ConfigK8sClient::K8sJsonAddRefs(
    Value::ConstMemberIterator& ref,
    Value::ConstMemberIterator& fq_name,
    Document& cass_dom,
    map<string, Value>& ref_map)
{
    // convert and set the name of the ref to add
    string ref_name_str = K8sNameConvert(
        ref->name.GetString(), ref->name.GetStringLength());
    // If we find the string "reference(s)" at the end of the name, replace with "ref(s)"
    auto find_reference = ref_name_str.rfind("_reference");
    if (find_reference != string::npos)
    {
        ref_name_str.erase(find_reference + 4);
        ref_name_str += 's';
    }

    if (ref->value.IsObject() && ref_name_str == "parent") 
    {
        // If this is a parent reference, need to set the parent_type
        auto ref_kind = ref->value.FindMember("kind");
        if (ref_kind != ref->value.MemberEnd())
        {
            string parent_type_str = K8sNameConvert(
                ref_kind->value.GetString(), ref_kind->value.GetStringLength());
            Value parent_type;

            parent_type.SetString(
                parent_type_str.c_str(), parent_type_str.length(),
                cass_dom.GetAllocator());
            cass_dom.AddMember("parent_type", parent_type, cass_dom.GetAllocator());
        }

        auto ref_uid = ref->value.FindMember("uid");
        if (ref_uid != ref->value.MemberEnd())
        {
            Value parent_uuid;
            parent_uuid.CopyFrom(ref_uid->value, cass_dom.GetAllocator());
            cass_dom.AddMember("parent_uuid", parent_uuid, cass_dom.GetAllocator());
        }

        auto ref_name = ref->value.FindMember("name");
        if (ref_name != ref->value.MemberEnd())
        {
            string parent_ref_to = FqNameToParentRefString(fq_name->value);
            Value parent_name;
            parent_name.SetString(parent_ref_to.c_str(), cass_dom.GetAllocator());
            cass_dom.AddMember("parent_name", parent_name, cass_dom.GetAllocator());
        }
        return;
    }

    // Value is an array. Create an array and add each array element to the dom.
    Value ref_value;
    auto ref_value_ptr = ref_map.find(ref_name_str);
    if (ref_value_ptr != ref_map.end())
    {
        ref_value = ref_value_ptr->second;
    }
    else
    {
        // Only do this if needed since it does an allocation
        ref_value.SetArray();
    }

    if (ref->value.IsObject())
    {
        Value ref_object = K8sJsonCreateRef(&(ref->value), cass_dom);
        // add the new array element to the array
        ref_value.PushBack(ref_object, cass_dom.GetAllocator());
        ref_map[ref_name_str] = ref_value;
        return;
    }
    else if (ref->value.IsArray()) 
    {
        for (const Value* ref_info = ref->value.GetArray().Begin();
             ref_info != ref->value.GetArray().End();
             ++ref_info)
        {
            Value ref_info_val = K8sJsonCreateRef(ref_info, cass_dom);
            // add the new array element to the ref array
            ref_value.PushBack(ref_info_val, cass_dom.GetAllocator());
        }

        // add the array to the ref_val
        ref_map[ref_name_str] = ref_value;
        return;
    }
    else
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, "K8S SM: Ref syntax error for ref " + ref_name_str);
    }
}

void ConfigK8sClient::K8sJsonConvert(
    const Document& dom, Document& cass_dom)
{
    assert(dom.IsObject());

    // Rename kind to type
    Value::ConstMemberIterator kind = dom.FindMember("kind");
    string type_val_str =
        ConfigK8sClient::K8sNameConvert(
            kind->value.GetString(), kind->value.GetStringLength());
    Value type_val;
    type_val.SetString(
        type_val_str.c_str(), type_val_str.length(), cass_dom.GetAllocator());
    cass_dom.SetObject();
    cass_dom.AddMember("type", type_val, cass_dom.GetAllocator());

    // Add the displayName
    Value::ConstMemberIterator metadata = dom.FindMember("metadata");
    Value::ConstMemberIterator annotations = dom.MemberEnd();
    if (metadata != dom.MemberEnd()) {
        annotations = metadata->value.FindMember("annotations");
        if (annotations != metadata->value.MemberEnd()) {
            Value::ConstMemberIterator display_name =
                annotations->value.FindMember(
                    "core.contrail.juniper.net/display-name");
            if (display_name != annotations->value.MemberEnd()) {
                Value display_name_val(
                    display_name->value, cass_dom.GetAllocator());
                cass_dom.AddMember(
                    "display_name", display_name_val, cass_dom.GetAllocator());
            }
        }
    }

    // TODO: Remove when fqName moved to spec.
    // Add and rename fqName
    Value::ConstMemberIterator status = dom.FindMember("status");
    Value::ConstMemberIterator fq_name = status->value.FindMember("fqName");
    if (fq_name != status->value.MemberEnd()) {
        Value fq_name_val(fq_name->value, cass_dom.GetAllocator());
        cass_dom.AddMember("fq_name", fq_name_val, cass_dom.GetAllocator());
    }

    // Add uuid
    Value::ConstMemberIterator uid = metadata->value.FindMember("uid");
    string uuid_string;
    if (uid != metadata->value.MemberEnd()) {
        uuid_string = uid->value.GetString();
    }
    Value uuid_val(uid->value, cass_dom.GetAllocator());
    cass_dom.AddMember("uuid", uuid_val, cass_dom.GetAllocator());
    Value::ConstMemberIterator uuid = dom.FindMember("uuid");

    // build idperm object and add description
    Value idperms_val;
    idperms_val.SetObject();
    if (metadata != dom.MemberEnd() &&
        annotations != metadata->value.MemberEnd())
    {
        Value::ConstMemberIterator description =
            annotations->value.FindMember(
                "core.contrail.juniper.net/description");
        if (description != annotations->value.MemberEnd() &&
            !description->value.IsNull())
        {
            Value description_val(description->value, cass_dom.GetAllocator());
            idperms_val.AddMember(
                "description", description_val, cass_dom.GetAllocator());
        }
    }

    // add created time to idperm
    if (metadata != dom.MemberEnd()) {
        Value::ConstMemberIterator created =
            metadata->value.FindMember("creationTimestamp");
        if (created != metadata->value.MemberEnd() &&
            !created->value.IsNull())
        {
            Value created_val(created->value, cass_dom.GetAllocator());
            idperms_val.AddMember(
                "created", created_val, cass_dom.GetAllocator());
        }
    }

    if (!uuid_string.empty()) {
        // convert string UUID into binary UUID
        unsigned long long longs[2] = {0};
        UUIDToLongLongs(uuid_string, longs);

        // create uuid object (binary) and add most signifigant 8 bytes
        Value idperm_uuid;
        idperm_uuid.SetObject();
        Value uuid_mslong_val;
        uuid_mslong_val.SetUint64(longs[0]);
        idperm_uuid.AddMember(
            "uuid_mslong", uuid_mslong_val, cass_dom.GetAllocator());

        // add least significant 8 bytes to uuid object
        Value uuid_lslong_val;
        uuid_lslong_val.SetUint64(longs[1]);
        idperm_uuid.AddMember(
            "uuid_lslong", uuid_lslong_val, cass_dom.GetAllocator());

        // finally, add uuid to id_perms
        idperms_val.AddMember("uuid", idperm_uuid, cass_dom.GetAllocator());
    }

    // idperms must have the enable flag set
    Value enable;
    enable.SetString("true", cass_dom.GetAllocator());
    idperms_val.AddMember("enable", enable, cass_dom.GetAllocator());

    // add id_perms to the dom
    cass_dom.AddMember("id_perms", idperms_val, cass_dom.GetAllocator());

    // Add annotation properties
    Value key_value_pairs;
    key_value_pairs.SetArray();
    if (metadata != dom.MemberEnd() && annotations != metadata->value.MemberEnd())
    {
        for(auto annotation = annotations->value.MemberBegin();
            annotation != annotations->value.MemberEnd();
            ++annotation)
        {
            string annotation_name = annotation->name.GetString();
            if (annotation_name.find("/") != string::npos)
            {
                // skip all system properties like
                // "core.juniper.net/description"
                continue;
            }
            // For each annotation, create a key/value pair object
            Value key_value_pair;
            Value key;
            Value value;
            key.CopyFrom(annotation->name, cass_dom.GetAllocator());
            value.CopyFrom(annotation->value, cass_dom.GetAllocator());
            key_value_pair.SetObject();
            key_value_pair.AddMember("key", key, cass_dom.GetAllocator());
            key_value_pair.AddMember("value", value, cass_dom.GetAllocator());
            // Finally, add it to the key_value_pair array
            key_value_pairs.PushBack(key_value_pair, cass_dom.GetAllocator());
        }
        if (key_value_pairs.GetArray().Size() > 0)
        {
            Value annotation;
            annotation.SetObject();
            annotation.AddMember("key_value_pair", key_value_pairs, cass_dom.GetAllocator());
            cass_dom.AddMember("annotations", annotation, cass_dom.GetAllocator());
        }
    }

    // Save refs into map, to be added all at once.
    // ref_name -> ref
    std::map< string, Value > ref_map;

    // Look for properties in the status.
    for(auto status_member = status->value.MemberBegin();
        status_member != status->value.MemberEnd();
        ++status_member)
    {
        string member_name = status_member->name.GetString();
        if (member_name == "state")
        {
            // ignore this field
            continue;
        }
        if (member_name.rfind("Reference") != string::npos) {
            // Add refs specially
            ConfigK8sClient::K8sJsonAddRefs(status_member, fq_name, cass_dom, ref_map);
        }
        else
        {
            // Add non-ref properties
            ConfigK8sClient::K8sJsonMemberConvert(
                status_member, cass_dom, cass_dom.GetAllocator());
        }

    }

    // Iterate through the spec.
    // Add all the values not duplicated from the status.
    auto spec = dom.FindMember("spec");
    if (spec != dom.MemberEnd() && !spec->value.IsNull()) {
        // TODO: Remove conditional when fqName moved to spec.
        if (fq_name == status->value.MemberEnd()) {
            fq_name = spec->value.FindMember("fqName");
        }

        for (auto spec_member = spec->value.MemberBegin();
            spec_member != spec->value.MemberEnd();
            ++spec_member)
        {
            std::string spec_member_name = spec_member->name.GetString();

            if (spec_member_name == "parent" ||
                spec_member_name.rfind("Reference") != string::npos)
            {
                ConfigK8sClient::K8sJsonAddRefs(spec_member, fq_name, cass_dom, ref_map);
            }
            else if (spec_member_name == "fqName")
            {
                Value fq_name_val(spec_member->value, cass_dom.GetAllocator());
                cass_dom.AddMember("fq_name", fq_name_val, cass_dom.GetAllocator());
            }
            else
            {
                // Add non-ref properties (not overriding anything set by status).
                ConfigK8sClient::K8sJsonMemberConvert(
                    spec_member, cass_dom, cass_dom.GetAllocator());
            }
        }
    }

    // Add all the discovered refs
    for(auto ref = ref_map.begin(); ref != ref_map.end(); ++ref)
    {
        Value ref_name;
        ref_name.SetString(ref->first.c_str(), ref->first.length(), cass_dom.GetAllocator());
        cass_dom.AddMember(ref_name, ref->second, cass_dom.GetAllocator());
    }
}

bool ConfigK8sClient::InitRetry() {
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, "K8S SM: DB Init Retry");
    // If reinit is triggered, return false to abort connection attempt
    if (mgr()->is_reinit_triggered()) return false;
    usleep(GetInitRetryTimeUSec());
    return true;
}

bool ConfigK8sClient::BulkDataSync()
{
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                        "K8S SM: BulkDataSync Started");
    bulk_sync_status_ = num_workers_;
    uuid_reader_->Set();
    return true;
}

void ConfigK8sClient::BulkSyncDone()
{
    long num_config_readers_still_processing =
        bulk_sync_status_.fetch_and_decrement();
    if (num_config_readers_still_processing == 1)
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: BulkSyncDone by all readers");
        mgr()->EndOfConfig();
    }
    else
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: One reader finished BulkSync");
    }
}

void ConfigK8sClient::PostShutdown()
{
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                        "K8S SM: Post shutdown during re-init");
    STLDeleteValues(&partitions_);
    ClearFQNameCache();
}

ConfigK8sPartition *
ConfigK8sClient::GetPartition(const string &uuid)
{
    int worker_id = HashUUID(uuid);
    return partitions_[worker_id];
}

const ConfigK8sPartition *
ConfigK8sClient::GetPartition(const string &uuid) const
{
    int worker_id = HashUUID(uuid);
    return partitions_[worker_id];
}

const ConfigK8sPartition *
ConfigK8sClient::GetPartition(int worker_id) const
{
    assert(worker_id < num_workers_);
    return partitions_[worker_id];
}

int ConfigK8sClient::HashUUID(const string &uuid_str) const
{
    boost::hash<string> string_hash;
    return string_hash(uuid_str) % num_workers_;
}

void ConfigK8sClient::EnqueueUUIDRequest(string oper,
                                         string uuid,
                                         string value)
{
    // If non-object JSON is received, log a warning and return.
    Document dom;
    dom.Parse<0>(value.c_str());
    if (!dom.IsObject())
    {
        CONFIG_CLIENT_WARN(
            ConfigClientMgrWarning,
            "K8S SM: Received non-object json. uuid: " + uuid + " value: "
            + value + ". Skipping");
        return;
    }

    // Ignore all object without the state "Success"
    Value::MemberIterator status = dom.FindMember("status");
    if (status == dom.MemberEnd()) {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrDebug,
            "K8S SM: Received json object without status field. uuid: " + uuid
            + " value: " + value + ". Skipping");
        return;
    }

    Value::MemberIterator state = status->value.FindMember("state");
    if (state == status->value.MemberEnd()) {
        CONFIG_CLIENT_WARN(
            ConfigClientMgrWarning,
            "K8S SM: Received json object without state. uuid: " + uuid
            + " value: " + value + ". Skipping");
        return;
    }

    static const char success_cstr[] = "Success";
    if (strncmp(state->value.GetString(), success_cstr, sizeof(success_cstr)) != 0 ) {
        CONFIG_CLIENT_DEBUG(
            ConfigClientMgrWarning,
            "K8S SM: Received json object with Status != Success. uuid: "
            + uuid + " value: " + value + ". Skipping");
        return;
    }

    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, "K8S SM: BEFORE CONVERSION: " + value);

    // Translate from K8s DOM to internal (Cassandra) format.
    Document cass_json;
    K8sJsonConvert(dom, cass_json);

    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, "K8S SM: AFTER CONVERSION: " + JsonToString(cass_json));

    if (oper == "ADDED" || oper == "MODIFIED")
    {
        // Add to FQName cache if not present
        Value::ConstMemberIterator type = cass_json.FindMember("type");
        if (type == cass_json.MemberEnd())
        {
            CONFIG_CLIENT_WARN(ConfigClientMgrWarning, "K8S SM: Received "
                                                        "json object without type specified. uuid: " +
                                                        uuid + " object: " + JsonToString(cass_json) + ". Skipping");
            return;
        }
        string type_str = type->value.GetString();
        Value::ConstMemberIterator fq_name = cass_json.FindMember("fq_name");
        if (fq_name == cass_json.MemberEnd() || fq_name->value.IsNull() || !fq_name->value.IsArray())
        {
            CONFIG_CLIENT_WARN(ConfigClientMgrWarning, "K8S SM: Received "
                                                        "json object without fq_name specified. uuid: " +
                                                        uuid + " object: " + JsonToString(cass_json) + ". Skipping");
            return;
        }
        if (FindFQName(uuid) == "ERROR")
        {
            string fq_name_str = FqNameToString(fq_name->value);
            AddFQNameCache(uuid, type_str, fq_name_str);
            CONFIG_CLIENT_DEBUG(
                ConfigClientMgrDebug,
                "AddFQNameCache(" + uuid + ',' + type_str + ',' + fq_name_str + ')');
        }
    }
    else if (oper == "DELETED")
    {
        // Invalidate cache
        InvalidateFQNameCache(uuid);
    }

    // Request has the uuid
    ObjectProcessReq *req = new ObjectProcessReq(oper, uuid, ConfigK8sClient::JsonToString(cass_json));

    // GetPartition uses the uuid so that the same
    // partition is returned for different requests on the
    // same UUID.
    // Note that you MUST requeue if requests come in out of order!
    GetPartition(uuid)->Enqueue(req);
}

void ConfigK8sClient::EnqueueDBSyncRequest(
    DomPtr dom_ptr)
{
    this->EnqueueUUIDRequest(
        "ADDED",
        K8sClient::UidFromObject(*dom_ptr),
        ConfigK8sClient::JsonToString(*dom_ptr));
}

bool ConfigK8sClient::UUIDReader()
{
    // Iterate through all of the object types
    for (ConfigClientManager::ObjectTypeList::const_iterator it =
             mgr()->config_json_parser()->ObjectTypeListToRead().begin();
         it != mgr()->config_json_parser()->ObjectTypeListToRead().end();
         it++)
    {
        // Convert cassandra type name to Kubernetes
        string kind = CassTypeToK8sKind(*it);

        // First make sure the backend supports this type
        if (k8s_client_->kindInfoMap().find(kind) == k8s_client_->kindInfoMap().end())
        {
            CONFIG_CLIENT_WARN(ConfigClientMgrWarning, "K8S SM: Type "
                               + *it + " not supported. Skipping");
            continue;
        }

        /**
         * Ensure that UUIDReader task aborts on reinit trigger.
         */
        if (mgr()->is_reinit_triggered())
        {
            CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                                "K8S SM: Abort UUID reader on reinit trigger");
            return true;
        }

        // Perform the bulk get for this type.
        int resp = k8s_client_->BulkGet(
            kind, boost::bind(&ConfigK8sClient::EnqueueDBSyncRequest, this, _1));
        if (resp >= 300 || resp < 200)
        {
            /**
             * RPC failure. Connection down.
             */
            HandleK8sConnectionStatus(false);
            usleep(GetInitRetryTimeUSec());
        }
    } //for

    // At the end of task trigger
    BOOST_FOREACH (ConfigK8sPartition *partition, partitions_)
    {
        ObjectProcessReq *req = new ObjectProcessReq("EndOfConfig", "", "");
        partition->Enqueue(req);
    }

    return true;
}

bool ConfigK8sClient::IsTaskTriggered() const
{
    // If UUIDReader task has been triggered return true.
    if (uuid_reader_->IsSet())
    {
        return true;
    }

    /**
      * Walk the partitions and check if ConfigReader task has
      * been triggered in any of them. If so, return true.
      */
    BOOST_FOREACH (ConfigK8sPartition *partition, partitions_)
    {
        if (partition->IsTaskTriggered())
        {
            return true;
        }
    }
    return false;
}

bool ConfigK8sClient::UUIDToObjCacheShow(
    const string &search_string,
    int inst_num,
    const string &last_uuid,
    uint32_t num_entries,
    vector<ConfigDBUUIDCacheEntry> *entries) const
{
    return GetPartition(inst_num)->UUIDToObjCacheShow(search_string, last_uuid,
                                                      num_entries, entries);
}

bool ConfigK8sClient::IsListOrMapPropEmpty(const string &uuid_key,
                                           const string &lookup_key)
{
    return GetPartition(uuid_key)->IsListOrMapPropEmpty(uuid_key, lookup_key);
}

ConfigK8sPartition::ConfigK8sPartition(
    ConfigK8sClient *client, size_t idx)
    : config_client_(client), worker_id_(idx)
{
    int task_id = TaskScheduler::GetInstance()->GetTaskId("config_client::Reader");
    config_reader_.reset(new TaskTrigger(boost::bind(&ConfigK8sPartition::ConfigReader, this),
                                         task_id, worker_id_));
    task_id =
        TaskScheduler::GetInstance()->GetTaskId("config_client::ObjectProcessor");
    obj_process_request_queue_.reset(new WorkQueue<ObjectProcessReq *>(
        task_id, worker_id_, bind(&ConfigK8sPartition::ObjectProcessReqHandler, this, _1),
        WorkQueue<ObjectProcessReq *>::kMaxSize, 512));
}

ConfigK8sPartition::~ConfigK8sPartition()
{
    obj_process_request_queue_->Shutdown();
}

void ConfigK8sPartition::Enqueue(ObjectProcessReq *req)
{
    obj_process_request_queue_->Enqueue(req);
}

bool ConfigK8sPartition::ObjectProcessReqHandler(ObjectProcessReq *req)
{
    AddUUIDToProcessRequestMap(req->oper_, req->uuid_str_, req->value_);
    delete req;
    return true;
}

/**
  * Add the UUID key/value pair to the process list.
  */
void ConfigK8sPartition::AddUUIDToProcessRequestMap(const string &oper,
                                                    const string &uuid,
                                                    const string& value_str)
{
    pair<UUIDProcessRequestMap::iterator, bool> ret;
    bool trigger = uuid_process_request_map_.empty();

    boost::shared_ptr<UUIDProcessRequestType> req(
        new UUIDProcessRequestType(oper, uuid, value_str));
    ret = uuid_process_request_map_.insert(make_pair(client()->GetUUID(uuid), req));
    if (ret.second)
    {
        /**
          * UUID not present in uuid_process_request_map_.
          * If uuid_process_request_map_ is empty, trigger config_reader_
          * to process the uuids.
          */
        if (trigger)
        {
            config_reader_->Set();
        }
    }
    else
    {
        /**
          * UUID already present in uuid_process_request_map_. If operation is
          * DELETE preceeded by CREATE, remove from uuid_process_request_map_.
          * For other cases (CREATE/UPDATE) replace the entry with the
          * new value and oper.
          */
        if ((oper == "DELETED") &&
            (ret.first->second->oper == "ADDED"))
        {
            uuid_process_request_map_.erase(ret.first);
            client()->PurgeFQNameCache(uuid);
        }
        else
        {
            // Replace the entry that was found in the map
            req.reset();
            ret.first->second->oper = oper;
            ret.first->second->uuid = uuid;
            ret.first->second->value_str = value_str;
        }
    }
}

ConfigK8sPartition::UUIDCacheEntry::~UUIDCacheEntry()
{
}

void ConfigK8sPartition::FillUUIDToObjCacheInfo(const string &uuid,
                                                UUIDCacheMap::const_iterator uuid_iter,
                                                ConfigDBUUIDCacheEntry *entry) const
{
    entry->set_uuid(uuid);
    entry->set_timestamp(
        UTCUsecToString(uuid_iter->second->GetLastReadTimeStamp()));
    entry->set_fq_name(uuid_iter->second->GetFQName());
    entry->set_obj_type(uuid_iter->second->GetObjType());
    entry->set_json_str(uuid_iter->second->GetJsonString());
}

bool ConfigK8sPartition::UUIDToObjCacheShow(
    const string &search_string,
    const string &last_uuid,
    uint32_t num_entries,
    vector<ConfigDBUUIDCacheEntry> *entries) const
{
    uint32_t count = 0;
    regex search_expr(search_string);
    for (UUIDCacheMap::const_iterator it =
             uuid_cache_map_.upper_bound(last_uuid);
         count < num_entries && it != uuid_cache_map_.end(); it++)
    {
        if (regex_search(it->first, search_expr) ||
            regex_search(it->second->GetObjType(), search_expr) ||
            regex_search(it->second->GetFQName(), search_expr))
        {
            count++;
            ConfigDBUUIDCacheEntry entry;
            FillUUIDToObjCacheInfo(it->first, it, &entry);
            entries->push_back(entry);
        }
    }
    return true;
}

ConfigK8sPartition::UUIDCacheEntry *
ConfigK8sPartition::GetUUIDCacheEntry(const string &uuid)
{
    UUIDCacheMap::iterator uuid_iter = uuid_cache_map_.find(uuid);
    if (uuid_iter == uuid_cache_map_.end())
    {
        return NULL;
    }
    return uuid_iter->second;
}

ConfigK8sPartition::UUIDCacheEntry *
ConfigK8sPartition::GetUUIDCacheEntry(const string &uuid,
                                      const string &value,
                                      bool &is_new)
{
    UUIDCacheMap::iterator uuid_iter = uuid_cache_map_.find(uuid);
    if (uuid_iter == uuid_cache_map_.end())
    {
        /**
          * Cache entry not present. Create one.
          */
        UUIDCacheEntry *obj;
        obj = new UUIDCacheEntry(this,
                                 value,
                                 UTCTimestampUsec());
        /**
          * Insert cache entry into UUIDCacheMap.
          */
        string tmp_uuid = uuid;
        pair<UUIDCacheMap::iterator, bool> ret_uuid =
            uuid_cache_map_.insert(tmp_uuid, obj);
        assert(ret_uuid.second);
        uuid_iter = ret_uuid.first;

        /**
          * Indicate to calling function that cache has been
          * newly created.
          */
        is_new = true;
    }
    else
    {
        /**
          * Cache entry already present. Update LastReadTimestamp.
          */
        uuid_iter->second->SetLastReadTimeStamp(UTCTimestampUsec());
    }

    /**
      * Return the cache entry.
      */
    return uuid_iter->second;
}

bool ConfigK8sPartition::UUIDCacheEntry::ListOrMapPropEmpty(
    const string &prop) const
{
    PropEmptyMap::const_iterator it = prop_empty_map_.find(prop);
    if (it == prop_empty_map_.end())
    {
        return true;
    }
    return (it->second == false);
}

bool ConfigK8sPartition::GenerateAndPushJson(const string &uuid,
                                             Document &doc,
                                             bool add_change,
                                             UUIDCacheEntry *cache)
{
    // Get obj_type from cache.
    const string &obj_type = cache->GetObjType();

    // string to get the type field.
    string type_str;

    // Walk the document, remove unwanted properties and do
    // needed fixup for the others.
    Value::ConstMemberIterator itr = doc.MemberBegin();
    Document::AllocatorType &a = doc.GetAllocator();

    while (itr != doc.MemberEnd())
    {
        string key = itr->name.GetString();

        /**
          * Get the type string. This will be used as the key.
          * Also remove this field from the document.
          */
        if (key.compare("type") == 0)
        {
            type_str = itr->value.GetString();
            itr = doc.EraseMember(itr);
            continue;
        }

        string wrapper = client()->mgr()->config_json_parser()->GetWrapperFieldName(obj_type, key.c_str());
        if (!wrapper.empty())
        {
            /**
              * Handle prop_map and prop_list objects.
              * Need to indicate in cache if they are NULL.
              * To find if obj is prop_list or prop_map, we
              * use the WrapperFieldNames defined in schema.
              * Today they are present only for prop_list
              * and prop_map. If that changes, logic here has
              * to change to accommodate it.
              */

            // Get the propl/propm json Value
            Value &map_value = doc[key.c_str()];

            // Indicate in cache if propm/propl is empty
            cache->SetListOrMapPropEmpty(key,
                                         map_value.IsNull());
        }
        else if (key.compare("parent_type") == 0)
        {
            /**
              * Process parent_type. Need to change - to _.
              */

            string parent_type = doc[key.c_str()].GetString();
            replace(parent_type.begin(), parent_type.end(),
                    '-', '_');
            doc[key.c_str()].SetString(parent_type.c_str(), a);
        }
        else if (key.compare("bgpaas_session_attributes") == 0)
        {
            /**
              * Process bgpaas_session_attributes property.
              * Value needs to be set to "".
              */

            doc[key.c_str()].SetString("", a);
        }
        else if (key.find("_refs") != string::npos && add_change)
        {
            /**
              * For _refs, if attr is NULL,
              * replace NULL with "".
              * Also add fq_name to each _ref.
              * Deletes do not need manipulation of _refs as previous
              * create/update would have already formatted them.
              */

            // Determine if NULL attr needs to be processed
            string ref_type = key.substr(0, key.length() - 5);
            bool link_with_attr =
                client()->mgr()->config_json_parser()->IsLinkWithAttr(obj_type, ref_type);

            // Get a pointer to the _refs json Value
            Value *v = &(doc[key.c_str()]);

            assert(v->IsArray());
            for (SizeType i = 0; i < v->Size(); i++)
            {
                // Process NULL attr
                Value &va = (*v)[i];
                if (link_with_attr)
                {
                    if (va["attr"].IsNull())
                    {
                        va.RemoveMember("attr");
                        Value vm;
                        va.AddMember("attr", vm.SetObject(), a);
                    }
                }

                // If ref_fq_name is missing from doc, add the
                // string formatted fq_name.
                if (va.FindMember("to") == va.MemberEnd())
                {
                    auto ref_iter = va.FindMember("uuid");
                    string ref_uuid;
                    string ref_fq_name;
                    if (ref_iter != va.MemberEnd())
                    {
                        ref_uuid = ref_iter->value.GetString();
                        ref_fq_name = client()->FindFQName(ref_uuid);
                    }
                    else
                    {
                        v->GetArray().Erase(&va);
                        continue;
                    }

                    // If we get the response back "ERROR", it is likely that
                    // a request is being process out-of-order
                    // (e.g.-- global_system_config before referenced
                    // bgp_router).  In this case, we must requeue.
                    if (ref_fq_name == "ERROR")
                    {
                        CONFIG_CLIENT_WARN(
                            ConfigClientMgrDebug,
                            "Request to process object UUID " + uuid +
                            " before ref " + ref_uuid + ". Must requeue.");
                        string req = add_change ? "ADDED" : "MODIFIED";
                        this->Enqueue(
                            new ObjectProcessReq(req, uuid, cache->GetJsonString()));
                        return false;
                    }

                    CONFIG_CLIENT_DEBUG(
                        ConfigClientMgrDebug,
                        "FindFQName(" + uuid + ") == " + ref_fq_name);

                    Value vs1(ref_fq_name.c_str(), a);
                    va.AddMember("to", vs1, a);
                }
            }

            // For creates/updates, need to update cache json_str
            // with the new fixed ref_fq_names for _refs.
            // Remove existing reference in cache and create a new
            // ref with updated ref_fq_names.
            Document cacheDoc;
            string cache_json_str = cache->GetJsonString();
            cacheDoc.Parse<0>(cache_json_str.c_str());
            cacheDoc.RemoveMember(key.c_str());
            Value vr;
            Value vra;
            Value refVal;
            refVal.CopyFrom(*v, a);
            cacheDoc.AddMember(vr.SetString(key.c_str(), a),
                               refVal, a);
            StringBuffer sb;
            Writer<StringBuffer> writer(sb);
            cacheDoc.Accept(writer);
            string cache_str = sb.GetString();
            cache->SetJsonString(cache_str);
        }

        if (itr != doc.MemberEnd())
            itr++;
    }

    StringBuffer sb1;
    Writer<StringBuffer> writer1(sb1);
    Document refDoc;
    refDoc.CopyFrom(doc, refDoc.GetAllocator());
    refDoc.Accept(writer1);
    string refString = sb1.GetString();
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                        "K8S SM: JSON Doc fed to CJP: " + refString);

    ConfigCass2JsonAdapter ccja(uuid, type_str, doc);
    client()->mgr()->config_json_parser()->Receive(ccja, add_change);

    return true;
}

void ConfigK8sPartition::ProcessUUIDDelete(
    const string &uuid_key)
{

    /**
      * If FQName cache not present for the uuid, it is likely
      * a redundant delete since we remove it from the FQName
      * cache when a delete is processed. Ignore request.
      */
    if (client()->FindFQName(uuid_key) == "ERROR")
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: Nothing to delete");
        return;
    }

    /**
      * Get the cache entry for the uuid.
      * Assert if uuid is not present in the cache.
      */
    UUIDCacheMap::iterator uuid_iter = uuid_cache_map_.find(uuid_key);
    if (uuid_iter == uuid_cache_map_.end())
    {
        return;
    }
    UUIDCacheEntry *cache = uuid_iter->second;

    /**
      * For CREATES, we could get here in erroneous
      * cases as well. For instance, fq_name or type field not
      * present in the update received. For such cases,
      * only delete cache entry.
      * Get the cached json_str and create a JSON Document.
      * This will be sent to lower layers as a delete.
      */
    const string cache_json_str = cache->GetJsonString();
    Document delDoc;
    delDoc.Parse<0>(cache_json_str.c_str());

    /**
      * Fixup the JSON document and push it down
      * to lower layers.
      */
    GenerateAndPushJson(uuid_key,
                        delDoc,
                        false,
                        cache);

    /**
      * Delete the UUIDCacheEntry.
      */
    uuid_cache_map_.erase(uuid_iter);

    /**
      * Purge FQName cache entry.
      */
    client()->PurgeFQNameCache(uuid_key);
}

void ConfigK8sPartition::ProcessUUIDUpdate(const string &uuid,
                                           const string &value_str)
{
    /**
      * Create UUIDCacheEntry if not present. This will create a
      * JSON document from the value_str and store in cache. The
      * cache entry is then inserted in to the UUIDCacheMap.
      * If cache entry already present, update the timestamp.
      */
    bool is_new = false;
    UUIDCacheEntry *cache = GetUUIDCacheEntry(uuid,
                                              value_str,
                                              is_new);

    /**
      * Get the cached json_str and create a JSON Document.
      * This will be used to compare with the updated
      * JSON value_str received.
      * As part of the update, fields in cacheDoc that are
      * present in the received update will be removed.
      * Finally, it will contain the fields that are to be
      * deleted.
      */
    string cache_json_str = cache->GetJsonString();
    Document cacheDoc;
    cacheDoc.Parse<0>(cache_json_str.c_str());

    /**
      * Create a JSON Document from the received JSON value_str.
      * This will be updated to capture newly created and
      * updated fields. Properties that have not changed will be
      * removed from the document.
      */
    Document updDoc;
    updDoc.Parse<0>(value_str.c_str());
    string key;

    /**
      * If cache is new, we can send the cacheDoc as is down to
      * the IFMAP server to send as update.
      * If not, we compare the cached version with the updated
      * values and potentially generate two documents.
      * 1. One that contains thew newly added fields and the
      *    fields that were updated.
      * 2. One that contains the fields that were removed in the
      *    received update.
      */
    Value::ConstMemberIterator itr = updDoc.MemberBegin();
    while (itr != updDoc.MemberEnd())
    {

        key = itr->name.GetString();

        /**
          * For BOTH creates and updates.
          * Ignore if update is received in draft-mode state
          * and delete from cache.
          */
        if (key.compare("draft_mode_state") == 0)
        {
            string mode = itr->value.GetString();
            if (!mode.empty())
            {
                client()->PurgeFQNameCache(uuid);
                DeleteUUIDCacheEntry(uuid);
                return;
            }
            itr = updDoc.EraseMember(itr);
            continue;
        }

        /**
          * For new cache entries ONLY, populate fq_name and
          * obj_type fields in the ObjCache.
          * For updates/deletes, cache should already have
          * the fq_name and obj_type fields populated.
          */
        if (is_new)
        {
            if (key.compare("type") == 0)
            {
                string type = itr->value.GetString();
                cache->SetObjType(type);
            }
            else if (key.compare("fq_name") == 0)
            {
                cache->SetFQName(ConfigK8sClient::FqNameToString(itr->value));
            }
        }

        /**
          * For updates ONLY.
          * If cache has the field and if it has not been updated
          * remove from updDoc. Also, remove the field
          * from cacheDoc. Skip fq_name and obj_type.

          */
        if (!is_new && cacheDoc.HasMember(key.c_str()) &&
            key.compare("type") != 0 &&
            key.compare("fq_name") != 0)
        {
            if (cacheDoc[key.c_str()] == updDoc[key.c_str()])
            {
                itr = updDoc.EraseMember(itr);
            }
            assert(cacheDoc.RemoveMember(key.c_str()));
        }
        else
        {
            if (itr != updDoc.MemberEnd())
                itr++;
        }
    }

    /**
      * Now that the updates are ready, replace the json_str in
      * the cache with the updated value_str.
      */
    cache->SetJsonString(value_str);

    /**
      * Fixup the JSON document and push it down
      * to lower layers.
      * Send the creates and updates followed
      * by the deleted fields.
      * Deletes are only sent for UPDATES as there
      * may be properties that were removed in the
      * update.
      * For CREATES, there is nothing to delete
      * as they are just newly added to cache.
      * When adding/updating properties, if there is
      * an error, retry after a while.
      */
    GenerateAndPushJson(uuid,
                        updDoc,
                        true,
                        cache);
    if (!is_new)
    {
        GenerateAndPushJson(uuid,
                            cacheDoc,
                            false,
                            cache);
    }
}

bool ConfigK8sPartition::IsListOrMapPropEmpty(const string &uuid_key,
                                              const string &lookup_key)
{
    UUIDCacheMap::iterator uuid_iter = uuid_cache_map_.find(uuid_key);
    if (uuid_iter == uuid_cache_map_.end())
    {
        return true;
    }
    UUIDCacheEntry *cache = uuid_iter->second;

    return cache->ListOrMapPropEmpty(lookup_key);
}

bool ConfigK8sPartition::IsTaskTriggered() const
{
    return (config_reader_->IsSet());
}

bool ConfigK8sPartition::ConfigReader()
{
    CHECK_CONCURRENCY("config_client::Reader");

    int num_req_handled = 0;

    /**
      * Walk through the requests in uuid_process_request_map_ and process them.
      * uuid_process_request_map_ contains the response from K8S with the
      * uuid key-value pairs.
      * Config reader task should stop on reinit trigger
      */
    for (UUIDProcessRequestMap::iterator it = uuid_process_request_map_.begin(), itnext;
         it != uuid_process_request_map_.end() &&
         !client()->mgr()->is_reinit_triggered();
         it = itnext)
    {

        itnext = it;
        ++itnext;

        boost::shared_ptr<UUIDProcessRequestType> obj_req = it->second;

        if (obj_req->oper == "ADDED" || obj_req->oper == "MODIFIED")
        {
            ProcessUUIDUpdate(obj_req->uuid, obj_req->value_str);
        }
        else if (obj_req->oper == "DELETED")
        {
            ProcessUUIDDelete(obj_req->uuid);
        }
        else if (obj_req->oper == "EndOfConfig")
        {
            client()->BulkSyncDone();
        }
        RemoveObjReqEntry(obj_req->uuid);

        /**
          * Max. UUIDs to be processed in one config reader task
          * exectution is bound by kMaxRequestsToYield
          */
        if (++num_req_handled == client()->GetMaxRequestsToYield())
        {
            return false;
        }
    }

    // Clear the UUID read set if we are currently processing reinit request
    if (client()->mgr()->is_reinit_triggered())
    {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
                            "K8S SM: Clear UUID process set due to reinit");
        uuid_process_request_map_.clear();
    }
    assert(uuid_process_request_map_.empty());
    return true;
}

void ConfigK8sPartition::RemoveObjReqEntry(string &uuid)
{
    UUIDProcessRequestMap::iterator req_it =
        uuid_process_request_map_.find(client()->GetUUID(uuid));
    req_it->second.reset();
    uuid_process_request_map_.erase(req_it);
}
