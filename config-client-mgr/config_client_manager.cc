/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/request_pipeline.h>
#include <sstream>
#include <string>

#include "base/connection_info.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"
#include "config_k8s_client.h"
#include "config_client_log.h"
#include "config_client_log_types.h"
#include "config_client_show_types.h"
#include "config_factory.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace boost::assign;
using namespace std;

const set<string> ConfigClientManager::skip_properties =
    list_of("perms2")("draft_mode_state");
bool ConfigClientManager::end_of_rib_computed_;

int ConfigClientManager::GetNumConfigReader() {
    static bool init_ = false;
    static int num_config_readers = 0;

    if (!init_) {
        // XXX To be used for testing purposes only.
        char *count_str = getenv("CONFIG_NUM_WORKERS");
        if (count_str) {
            num_config_readers = strtol(count_str, NULL, 0);
        } else {
            num_config_readers = kNumConfigReaderTasks;
        }
        init_ = true;
    }
    return num_config_readers;
}

void ConfigClientManager::SetDefaultSchedulingPolicy() {
    static bool config_policy_set;
    if (config_policy_set)
        return;
    config_policy_set = true;
 
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Policy for config_client::Reader Task.
    TaskPolicy cassadra_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("config_client::Init")))
        (TaskExclusion(scheduler->GetTaskId("config_client::DBReader")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_reader_policy.push_back(
        TaskExclusion(scheduler->GetTaskId("config_client::ObjectProcessor"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("config_client::Reader"),
        cassadra_reader_policy);

    // Policy for config_client::ObjectProcessor Task.
    TaskPolicy cassadra_obj_process_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("config_client::Init")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_obj_process_policy.push_back(
                 TaskExclusion(scheduler->GetTaskId("config_client::Reader"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("config_client::ObjectProcessor"),
        cassadra_obj_process_policy);

    // Policy for config_client::DBReader Task.
    TaskPolicy fq_name_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("config_client::Init")))
        (TaskExclusion(scheduler->GetTaskId("config_client::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("config_client::DBReader"),
        fq_name_reader_policy);

    // Policy for config_client::Init process
    TaskPolicy cassandra_init_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("amqp::RabbitMQReader")))
        (TaskExclusion(scheduler->GetTaskId("config_client::ObjectProcessor")))
        (TaskExclusion(scheduler->GetTaskId("config_client::DBReader")))
        (TaskExclusion(scheduler->GetTaskId("config_client::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("config_client::Init"),
        cassandra_init_policy);

    // Policy for amqp::RabbitMQReader process
    TaskPolicy rabbitmq_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("config_client::Init")));
    scheduler->SetPolicy(scheduler->GetTaskId("amqp::RabbitMQReader"),
        rabbitmq_reader_policy);

    // Policy for k8s::K8sWatcher process
    TaskPolicy k8s_watcher_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("config_client::Init")))
        (TaskExclusion(scheduler->GetTaskId("config_client::DBReader")));
    scheduler->SetPolicy(scheduler->GetTaskId("k8s::K8sWatcher"),
        k8s_watcher_policy);
}

void ConfigClientManager::SetUp() {
    config_json_parser_.reset(ConfigFactory::Create<ConfigJsonParserBase>());
    config_json_parser_->Init(this);
    thread_count_ = GetNumConfigReader();
    end_of_rib_computed_at_ = UTCTimestampUsec();
    if (config_options_.config_db_use_k8s) {
        config_db_client_.reset(ConfigFactory::Create<ConfigK8sClient>
                                (this, evm_, config_options_,
                                 thread_count_));
        // TODO: Doing this makes logging more correct, but IFMap has hard-coded
        //       logic that expects IFMapOrigin::CASSANDRA that is hard to replace.
        //       It will be easiest to just remove Cassandra and RabbitMQ support
        //       altogether.
        // config_json_parser_->SetDbOrigin(IFMapOrigin::K8S);
    } else {
        config_db_client_.reset(
                ConfigFactory::Create<ConfigCassandraClient>(this, evm_,
                    config_options_, thread_count_));
        config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
    }
    SetDefaultSchedulingPolicy();

    int task_id;
    task_id = TaskScheduler::GetInstance()->GetTaskId("config_client::Init");

    init_trigger_.reset(new
         TaskTrigger(boost::bind(&ConfigClientManager::InitConfigClient, this),
         task_id, 0));

    reinit_triggered_ = false;
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        std::string hostname,
        std::string module_name,
        const ConfigClientOptions& config_options)
        : evm_(evm),
        generation_number_(0),
        hostname_(hostname), module_name_(module_name),
        config_options_(config_options) {
    end_of_rib_computed_ = false;
    SetUp();
}

ConfigClientManager::~ConfigClientManager() {
}

void ConfigClientManager::Initialize() {
    init_trigger_->Set();
}

ConfigDbClient *ConfigClientManager::config_db_client() const {
    return config_db_client_.get();
}

ConfigAmqpClient *ConfigClientManager::config_amqp_client() const {
    return config_amqp_client_.get();
}

bool ConfigClientManager::GetEndOfRibComputed() const {
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    return end_of_rib_computed_;
}

uint64_t ConfigClientManager::GetEndOfRibComputedAt() const {
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    return end_of_rib_computed_at_;
}

void ConfigClientManager::EnqueueUUIDRequest(string oper, string obj_type,
                                             string uuid_str) {
    config_db_client_->EnqueueUUIDRequest(oper, obj_type, uuid_str);
}

void ConfigClientManager::EndOfConfig() {
    {
        // Notify waiting caller with the result
        tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
        assert(!end_of_rib_computed_);
        end_of_rib_computed_ = true;
        cond_var_.notify_all();
        end_of_rib_computed_at_ = UTCTimestampUsec();
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: End of RIB computed and notification sent");
    }

    // Once we have finished reading the complete cassandra DB, we should verify
    // whether all DBEntries(node/link) are as per the new generation number.
    // The stale entry cleanup task ensure this.
    // There is no need to run stale clean up during first time startup
    if (GetGenerationNumber())
        config_json_parser()->EndOfConfig();

    process::ConnectionState::GetInstance()->Update();
}

// This function waits forever for bulk sync of cassandra config to finish
// The condition variable is triggered even in case of "reinit". In such a case
// wait is terminated and function returns.
// AMQP reader task starts consuming messages only after bulk sync.
// During reinit, the tight loop is broken by triggering the condition variable
void ConfigClientManager::WaitForEndOfConfig() {
    tbb::interface5::unique_lock<tbb::mutex> lock(end_of_rib_sync_mutex_);
    // Wait for End of config
    while (!end_of_rib_computed_) {
        cond_var_.wait(lock);
        if (is_reinit_triggered()) break;
    }
    string message;
    message = "Config Client Mgr SM: End of RIB notification received, "
              "re init triggered" + is_reinit_triggered()?"TRUE":"FALSE";
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug, message);
    return;
}

void ConfigClientManager::GetClientManagerInfo(
                                   ConfigClientManagerInfo &info) const {
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    info.end_of_rib_computed = end_of_rib_computed_;
    info.end_of_rib_computed_at = end_of_rib_computed_at_;
    info.end_of_rib_computed_at = UTCUsecToString(end_of_rib_computed_at_);
}

void ConfigClientManager::PostShutdown() {
    config_db_client_->PostShutdown();
    reinit_triggered_ = false;
    end_of_rib_computed_ = false;

    // All set to read next version of the config. Increment the generation
    IncrementGenerationNumber();

    // scoped ptr reset deletes the previous config db object
    // Create new config db client and amqp client
    // Delete of config db client object guarantees the flusing of
    // object uuid cache and uuid read request list.
    if (config_options_.config_db_use_k8s) {
        config_db_client_.reset(ConfigFactory::Create<ConfigK8sClient>
                                (this, evm_, config_options_,
                                 thread_count_));
    } else {
        config_db_client_.reset(ConfigFactory::Create<ConfigCassandraClient>
                                (this, evm_, config_options_,
                                thread_count_));
        config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
    }
    stringstream ss;
    ss << GetGenerationNumber();
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Post shutdown, next version of config: "
            + ss.str());
}

bool ConfigClientManager::InitConfigClient() {
    if (is_reinit_triggered()) {
        // "config_client::Init" task is mutually exclusive to
        // 1. FQName reader task
        // 2. Object UUID Table reader task
        // 3. AMQP reader task
        // 4. Object processing Work queue task
        // Due to this task policy, if the reinit task is running, it ensured
        // that above mutually exclusive tasks have finished/aborted
        // Perform PostShutdown to prepare for new connection
        // However, it is possible that these tasks have been scheduled
        // but yet to begin execution. For Task and WorkQueue events, their
        // destructor takes core of this but for TaskTrigger events, the
        // destructor will crash (See Bug #1786154) as it expects the task
        // to not be scheduled. Taking care of that case here by checking
        // if TaskTrigger events are scheduled (but not executing) and
        // return if they are so that this will be retried.
        if (config_db_client_->IsTaskTriggered()) {
            return false;
        }
        PostShutdown();
    }

    // Common code path for both init/reinit
    if (config_options_.config_db_use_k8s) {
        // For do the bulk gets first.
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Init Database");
        config_db_client_->InitDatabase();
        // Then start the watch threads.
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Start K8S Watcher");
        config_db_client_->StartWatcher();
    } else {
        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Start RabbitMqReader and init Database");
        config_amqp_client_->StartRabbitMQReader();

        CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Init Database");
        config_db_client_->InitDatabase();
    }

    if (is_reinit_triggered()) return false;
    return true;
}

void ConfigClientManager::ReinitConfigClient(
                        const ConfigClientOptions &config) {
    config_options_ = config;
    ReinitConfigClient();
}

void ConfigClientManager::ReinitConfigClient() {
    {
        // Wake up the amqp task waiting for EOR for config reading
        tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
        cond_var_.notify_all();
    }
    reinit_triggered_ = true;
    init_trigger_->Set();
    CONFIG_CLIENT_DEBUG(ConfigClientMgrDebug,
            "Config Client Mgr SM: Re init triggered!");
}
