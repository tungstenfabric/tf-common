//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#include "k8s_types.h"
#include "k8s_client.h"
#include "k8s_client_log.h"
#include "schema/vnc_cfg_types.h"

#include <boost/regex.hpp>
#include <sstream>

using namespace boost;
using namespace k8s::client;

K8sUrl::K8sUrl(
    const std::string& serviceUrl,
    const std::string& apiGroup,
    const std::string& apiVersion)
{
    Reset(serviceUrl, apiGroup, apiVersion);
}

void K8sUrl::Reset(const std::string& serviceUrl,
                 const std::string& apiGroup,
                 const std::string& apiVersion)
{
    // Parse service, port and server address
    try
    {
        regex parseUrlExpr("([a-z]+)://([a-zA-Z0-9\\.-]+)(:|)([0-9]+|)(/.*)(/|)");
        smatch what;
        regex_match(serviceUrl, what, parseUrlExpr);
        protocol_ = what[1];
        server_ = what[2];
        port_ = what[4];
        path_ = what[5];
    }
    catch(const std::exception e)
    {
        K8S_CLIENT_DEBUG(K8sDebug, "K8S CLIENT: Invalid serviceUrl: " << serviceUrl);
        throw e;
    }

    // Create the (sanitized) Service and API url's
    apiGroup_ = apiGroup;
    apiVersion_ = apiVersion;
    serverUrl_ = protocol_ + "://" + server_ + (port_.empty() ? "" : ":" + port_);
    apiPath_ = path_ + '/' + apiGroup_ + '/' + apiVersion_;
}
