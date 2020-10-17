//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#ifndef DATABASE_K8S_UTIL_H_
#define DATABASE_K8S_UTIL_H_

#include <string>
#include <boost/scoped_ptr.hpp>

#include <k8s_url.h>

namespace RestClient { class Connection; }

namespace k8s {
namespace client {

typedef boost::scoped_ptr<RestClient::Connection> ConnectionPtr;

/**
 * Convert the file extension into a key type string.
 * e.g.-- cert.der ==> "DER", cert.pem ==> "PEM"
 * Returns empty string on failure.
 */
std::string CertType(const std::string& caCertFile);

/**
 * Initialize a connection (cx)
 * Requires a string for which to create the connection.
 * caCertFile is only required for SSL.
 */
void InitConnection(ConnectionPtr& cx,
                    const K8sUrl& k8sUrl,
                    const std::string& caCertFile);

/**
 * Tell controlling process to reload configuration and ifmap data.
 */
void RequestResync();

} //namespace client
} //namespace k8s
#endif
