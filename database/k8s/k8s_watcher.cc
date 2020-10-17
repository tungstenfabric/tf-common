//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#include "k8s_watcher.h"
#include "k8s_types.h"
#include "k8s_client_log.h"
#include "k8s_util.h"
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <signal.h>

using namespace boost;
using namespace k8s::client;
using namespace contrail_rapidjson;

size_t k8s::client::K8sWatcherWriteCallback(
    void *data, size_t size, size_t nmemb, K8sWatcherResponse *userdata)
{
    size_t bytes = size * nmemb;
    try
    {
        // If we're stopping, just return quickly
        if (userdata->watcher->Stopping())
        {
            // Results in CURLE_WRITE_ERROR to caller.
            return 0;
        }
        // Add to the buffer
        userdata->body.append(static_cast<char*>(data), bytes);
        // If the last character is not a new line, wait for the rest.
        if ('\n' != *(userdata->body.end() - 1))
        {
            return bytes;
        }
        // We have at least one event to process.
        std::stringstream stream(userdata->body);
        std::string eventString;
        while (std::getline(stream, eventString))
        {
            Document eventDom;
            eventDom.Parse<0>(eventString.c_str());
            if (!eventDom.IsObject())
            {
                K8S_CLIENT_WARN(K8sWarning,
                    std::string("K8S CLIENT: Invalid JSON: ") +
                    eventString.c_str());
                return 0;
            }
            auto type = eventDom.FindMember("type");
            std::string typeStr =
                type != eventDom.MemberEnd() ?
                    eventDom.FindMember("type")->value.GetString() :
                    "";
            if (typeStr.empty() || typeStr == "ERROR")
            {
                K8S_CLIENT_WARN(K8sWarning,
                    std::string("K8S CLIENT: ") + userdata->watcher->name() + " error watch response: " +
                    eventString.c_str());
                userdata->lastResponse = eventString;
                return 0;
            }
            DomPtr objectDomPtr(new Document);
            auto object = eventDom.FindMember("object");
            objectDomPtr->CopyFrom(object->value, objectDomPtr->GetAllocator());

            // Pass type and object payload to callback
            userdata->watcher->watchCb()(typeStr, objectDomPtr);

            // Persist the version
            auto metadata = object->value.FindMember("metadata");
            auto resourceVersion = metadata->value.FindMember("resourceVersion");
            userdata->watcher->SetVersion(resourceVersion->value.GetString());
            K8S_CLIENT_DEBUG(K8sDebug,
                "K8S CLIENT: " << userdata->watcher->name() <<
                " version set to " << resourceVersion->value.GetString() << '.');
        }
        // Done with the string
        userdata->body.clear();
    }
    catch(std::exception e)
    {
        K8S_CLIENT_WARN(K8sWarning, std::string("K8S CLIENT: Unhandled exception: ") + e.what());
        return 0;
    }
    return bytes;
}

K8sWatcher::K8sWatcher(
    const K8sUrls& k8sUrls, const std::string& name,
    k8s::client::WatchCb watchCb, const std::string& caCertFile)
: k8sUrls_(k8sUrls), name_(name), watchCb_(watchCb), caCertFile_(caCertFile)
{}

void K8sWatcher::InitConnection()
{
    K8S_CLIENT_WARN(
        K8sWarning, string("K8S CLIENT: ") + name_ +
        " watch connecting to " + k8sUrl().serverUrl());

    // Create connection context
    k8s::client::InitConnection(cx_, k8sUrl(), caCertFile_);

    // Set the callback used to process response
    cx_->SetWriteFunction(
        reinterpret_cast<RestClient::WriteCallback>(K8sWatcherWriteCallback));
    // Create the response context to handle receiving streamed data.
    // Provide it with a back-pointer to this watch object.
    response_.reset(new K8sWatcherResponse(this));
}

void K8sWatcher::Watch(const std::string& version, size_t retryDelay)
{
    // Save the version (of the last bulk get).
    // Used to formulate the watch query.
    version_ = version;

    // Initialize the connection context
    InitConnection();

    K8S_CLIENT_DEBUG(K8sDebug, "K8S CLIENT: " << name_ <<
                     " watch started, version " << version << ".");

    while(true)
    {
        try
        {
            // Break if we're stopping.
            if (Stopping())
            {
                // Stop requested
                K8S_CLIENT_DEBUG(K8sDebug,
                    "K8S CLIENT: " << name_ << " watch stopping.");
                break;
            }

            // Initiate the watch.
            // The response structure will receive watch data via callback.
            cx_->get(watchPath(), *response_);

            // On success or timeout, restart watch immediately.  Otherwise, sleep.
            if (response_->code == 200 || response_->code == CURLE_OPERATION_TIMEDOUT)
            {
                continue;
            }

            if (response_->code == CURLE_WRITE_ERROR)
            {
                // 410 means the watch is out of sync and a new bulk-sync is required.
                if (response_->lastResponse.rfind("\"code\":410") != std::string::npos)
                {
                    K8S_CLIENT_WARN(
                        K8sWarning, string("K8S CLIENT: ") + name_ +
                        " watch received 410 error, database out of sync: " +
                        response_->body);

                    // Kick off database re-initialization.
                    k8s::client::RequestResync();
                    break;
                }
            }

            K8S_CLIENT_WARN(
                K8sWarning, string("K8S CLIENT: ") + name_ +
                " watch failed, " + response_->body + ", " +
                k8sUrl().apiUrl());

            // wait and try another server
            boost::this_thread::sleep(boost::posix_time::seconds(retryDelay));
            k8sUrls_.rotate();
            InitConnection();
        }
        catch(const std::exception& e)
        {
            // Log termination request and exit
            K8S_CLIENT_WARN(
                K8sWarning, string("K8S CLIENT: ") + name_ +
                " watch error: " + e.what());
            break;
        }
    }
}

void K8sWatcher::Terminate()
{
    if (cx_)
    {
        if (threadPtr_)
        {
            K8S_CLIENT_DEBUG(
                K8sDebug, "K8S CLIENT: " << name_ << " watch terminated.");
            threadPtr_->interrupt();
            threadPtr_->join();
            threadPtr_.reset();
        }
        cx_.reset();
    }
}

void K8sWatcher::StartWatch(const std::string& version, size_t retryDelay)
{
    if (threadPtr_)
    {
        K8S_CLIENT_DEBUG(
            K8sDebug, "K8S CLIENT: " << name_ << " watch thread already running.");
        return;
    }

    threadPtr_.reset(new boost::thread(boost::bind(&K8sWatcher::Watch,
                     this, version, retryDelay)));
    K8S_CLIENT_DEBUG(
        K8sDebug, "K8S CLIENT: " << name_ << " watch thread started.");
}

void K8sWatcher::StopWatch()
{
    Terminate();
}

K8sWatcher::~K8sWatcher()
{
    Terminate();
}
