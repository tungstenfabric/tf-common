//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_K8S_WATCHER_H_
#define DATABASE_K8S_WATCHER_H_

#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>

#include <rapidjson/document.h>
#include <restclient-cpp/connection.h>

#include "k8s_url.h"
#include "k8s_client_types.h"

namespace k8s {
namespace client {

class K8sWatcher;

/**
 * K8sWatcherResponse maintains the context used for receiving streaming data.
 */
struct K8sWatcherResponse : public RestClient::Response
{
    /**
     * @brief Propagate watcher to write callback via response context.
     */
    K8sWatcherResponse(K8sWatcher *w) : watcher(w)
    {}
    K8sWatcher *watcher;
    std::string lastResponse;
};

/**
 * K8sWatcher watches for change events on a particular kind.
 * It maintains its own connection to the K8s API server, can
 * can be terminated on demand.
 */
class K8sWatcher {
public:
    /**
     * @brief Constructs a K8sWatcher.
     * @param k8sUrl is the K8s API server URL.
     * @param name Kubernetes type name to watch, like "projects".
     * @param watchCb Callback to invoke when an event takes place.
     * @param caCertFile CA certs file path to use for HTTPS.
     */
    K8sWatcher(
        const K8sUrls& k8sUrls, const std::string& name,
        k8s::client::WatchCb watchCb, const std::string& caCertFile = "");
    /**
     * @brief Destructor.  Implicitly stops the watcher.
     */
    virtual ~K8sWatcher();

    /**
     * @brief Establishes the connection to the server, and starts watching
     *        from a specified version.
     * @param Version string from the last bulk get for this (type) name.
     * @param Number of seconds to wait after a connection failure to
     *               try to re-establish the connection.
     */
    void Watch(const std::string& version = "", size_t retryDeley = 60);
    /**
     * @brief Stops watching.
     */
    void Terminate();

    /**
     * @brief Starts a watch thread
     * @param Version string from the last bulk get for this (type) name.
     * @param Number of seconds to wait after a connection failure to
     *               try to re-establish the connection.
     */
    void StartWatch(const std::string& version = "", size_t retryDelay = 10);
    /**
     * @brief Stops a watch thread
     */
    void StopWatch();
    /**
     * @brief: Check if the watcher is stopping.
     */
    bool Stopping() { return threadPtr_->interruption_requested(); }

    const K8sUrl& k8sUrl() const { return k8sUrls_.k8sUrl(); }
    const std::string& name() const { return name_; }
    const std::string& version() const { return version_; }
    void SetVersion(const std::string& version) { version_ = version; }
    WatchCb watchCb() const { return watchCb_; }

protected:
    K8sUrls k8sUrls_;
    const std::string name_;
    WatchCb watchCb_;
    const std::string caCertFile_;
    boost::scoped_ptr<RestClient::Connection> cx_;
    std::string version_;
    boost::scoped_ptr<K8sWatcherResponse> response_;
    boost::scoped_ptr<boost::thread> threadPtr_;

    /**
     * @brief: Compute the watch path
     */
    std::string watchPath() {
        return k8sUrl().namePath(name_) + "?watch=1&resourceVersion=" + version_;
    }

    /**
     * @brief: (Re-)initialize a connection (cx_).
     */
    void InitConnection();
};

/**
 * Callback to stream data from the get request.
 */
size_t K8sWatcherWriteCallback(
    void *data, size_t size, size_t nmemb, K8sWatcherResponse *userdata);

} //namespace client
} //namespace k8s
#endif