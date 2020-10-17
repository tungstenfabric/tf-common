//
// Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
//

#include "k8s_util.h"

#include <restclient-cpp/connection.h>

#include <sys/types.h>
#include <signal.h>

namespace k8s {
    namespace client {
        size_t Timeout = 5;
    }
}

std::string k8s::client::CertType(const std::string& caCertFile)
{
    std::string type;
    auto extension = caCertFile.rfind('.');
    type = caCertFile.substr(extension + 1);
    for (auto i = type.begin(); i != type.end(); i++)
    {
        *i = toupper(*i);
    }
    return type;
}

void k8s::client::InitConnection(k8s::client::ConnectionPtr& cx,
                                 const k8s::client::K8sUrl& k8sUrl,
                                 const std::string& caCertFile)
{
    // Create connection context
    cx.reset(new RestClient::Connection(k8sUrl.serverUrl()));

    // Set I/O timeout
    cx->SetTimeout(k8s::client::Timeout);

    // Set SSL options if enabled
    if (k8sUrl.encrypted())
    {
        std::string certType = k8s::client::CertType(caCertFile);
        cx->SetCertPath(caCertFile);
        cx->SetKeyPath(caCertFile);
        cx->SetCertType(certType);
    }
}

void k8s::client::RequestResync()
{
    kill(getpid(), SIGUSR1);
}
