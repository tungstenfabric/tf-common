//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_K8S_CLIENT_H_
#define DATABASE_K8S_CLIENT_H_

#include <memory>
#include <string>
#include <functional>
#include <map>
#include <vector>

#include <rapidjson/document.h>
#include <restclient-cpp/restclient.h>

#include "k8s_url.h"
#include "k8s_watcher.h"
#include "k8s_client_types.h"

namespace k8s {
namespace client {

/**
 * K8sClient is the Kubernetes client that is used to create and maintain connection
 * to the Kubernetes server.
 * The methods of the client can be used to perform Kubernetes operations.
 * Control node is only interested in the reading data from or watching
 * changes on a specific key or directory in Kubernetes and hence only those
 * operations are implemented here.
 */
class K8sClient {
public:
    /**
     * Constructor that creates a Kubernetes client object.
     * @param k8sUrls    Service address information for the
     *                   Kubernetes servers.
     * @param caCertFile CA cert file path to use for HTTPS.
     *                   Extension must be the type of cert.
     *                   e.g. "/path/cert.pem" or "/path/cert.p12"
     * @param rotate     When there are multiple k8sUrl endpoints,
     *                   which one to use first if rotating.
     * @param fetchLimit Maximum number of items to receive at once
     *                   when doing a get.
     */
    K8sClient(const std::vector<K8sUrl> &k8sUrls,
              const std::string &caCertFile,
              size_t rotate=0,
              size_t fetchLimit=defaultFetchLimit);

    /**
     * Destructor
     */
    virtual ~K8sClient();

    /**
     * Initialize the client by getting information on the types supported
     * by this apiGroup.  Returns 0 if initialization is successful,
     * otherwise returns non-zero.
     */
    virtual int Init();

    /**
     * Default maximum number of items to fetch at a time.
     */
    static const size_t defaultFetchLimit=500;

    /**
     * Get all Kubernetes objects of a particular type.
     * Blocks until all the data is retrieved.
     * Returns HTTP response code.  Does not throw.
     * @param kind Type name to get (e.g.-- project)
     * @param getCb Callback to invoke for each object that is retrieved.
     *              Takes as an argument a DomPtr.
     */
    virtual int BulkGet(const std::string &kind, GetCb getCb);

    /**
     * Watch for changes for a particular type since the last BulkGet.
     * Processing will continue in the background.
     * @param kind kind to watch (e.g.-- "VirtualMachine")
     * @param watchCb Callback to invoke for each object that is retrieved.
     *                Takes as arguments the type and (ADDED/MODIFIED/DELETED)
     *                and the DomPtr for the obect.
     */
    virtual void StartWatch(const std::string &kind,
                            WatchCb watchCb,
                            size_t retryDelay = 10);

    /**
     * Watch for changes for all types since the last BulkGet (for that type).
     * @param watchCb Callback to invoke for each object that is retrieved.
     *                Takes as arguments the type and (ADDED/MODIFIED/DELETED)
     *                and the DomPtr for the obect.
     * @param retryDelay Time to wait between reconnect attempts.
     */
    virtual void StartWatchAll(WatchCb watchCb, size_t retryDelay = 10);

    /**
     * Stop a particular watch request.
     */
    virtual void StopWatch(const std::string &kind);

    /**
     * Stop all watch requests that are running.
     */
    virtual void StopWatchAll();

    /**
     * Getters
     */
    const K8sUrls& k8sUrls() const {
        return k8sUrls_;
    }
    const K8sUrl& k8sUrl() const {
        return k8sUrls_.k8sUrl();
    }
    std::vector<Endpoint> endpoints() {
        return endpoints_;
    }
    std::string caCertFile() const {
        return caCertFile_;
    }
    size_t fetchLimit() const {
        return fetchLimit_;
    }

    // Get UUID from DOM of an object.
    static std::string UidFromObject(const contrail_rapidjson::Document& dom);

    /**
     * Types
     */
    struct KindInfo {
        std::string             name;
        std::string             singularName;
        bool                    namespaced;
        std::string             kind;
        std::string             resourceVersion;
        WatcherPtr              watcher;
    };
    typedef std::map<const std::string, KindInfo> KindInfoMap;

    const KindInfoMap& kindInfoMap() const {
        return kindInfoMap_;
    }

protected:
    // Location of the K8s API server
    K8sUrls k8sUrls_;
    std::vector<Endpoint> endpoints_;

    // Configuration options
    const std::string caCertFile_;
    size_t fetchLimit_;
    std::string fetchLimitString_;

    // Connection
    boost::scoped_ptr<RestClient::Connection> cx_;

    // Type information, indexed by kind
    KindInfoMap kindInfoMap_;
};

} //namespace client
} //namespace k8s
#endif
