//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_K8S_CLIENT_TYPES_H_
#define DATABASE_K8S_CLIENT_TYPES_H_

#include <boost/shared_ptr.hpp>
#include <boost/functional.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "rapidjson/document.h"

namespace k8s {
namespace client {

/**
 * Common Types
 */
typedef boost::shared_ptr<contrail_rapidjson::Document> DomPtr;
typedef boost::function<void (std::string type, DomPtr object)> WatchCb;

typedef boost::function<void (DomPtr object)> GetCb;

class K8sWatcher;
typedef boost::shared_ptr<K8sWatcher> WatcherPtr;

typedef boost::asio::ip::tcp::endpoint Endpoint;

} //namespace client
} //namespace k8s
#endif
