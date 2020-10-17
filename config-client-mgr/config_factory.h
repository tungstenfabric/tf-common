/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __CONFIG__CONFIG_FACTORY_H__
#define __CONFIG__CONFIG_FACTORY_H__

#include <string>
#include <vector>

#include <boost/function.hpp>
#include "base/factory.h"

namespace cass { namespace cql { class CqlIf; } }
using cass::cql::CqlIf;

namespace k8s { namespace client { class K8sUrl; class K8sClient; } }
using k8s::client::K8sUrl;
using k8s::client::K8sClient;

class ConfigAmqpChannel;
class ConfigCassandraClient;
class ConfigCassandraPartition;
class ConfigK8sClient;
class ConfigK8sPartition;
class ConfigClientManager;
class ConfigJsonParserBase;
struct ConfigClientOptions;
class EventManager;

class ConfigFactory : public Factory<ConfigFactory> {
    FACTORY_TYPE_N0(ConfigFactory, ConfigAmqpChannel);
    FACTORY_TYPE_N0(ConfigFactory, ConfigJsonParserBase);
    FACTORY_TYPE_N4(ConfigFactory, ConfigCassandraClient, ConfigClientManager *,
                    EventManager *, const ConfigClientOptions &,
                    int);
    FACTORY_TYPE_N2(ConfigFactory, ConfigCassandraPartition,
                    ConfigCassandraClient *, size_t);
    FACTORY_TYPE_N7(ConfigFactory, CqlIf, EventManager *,
                    const std::vector<std::string> &, int, const std::string &,
                    const std::string &, bool, const std::string &);
    FACTORY_TYPE_N4(ConfigFactory, ConfigK8sClient, ConfigClientManager *,
                    EventManager *, const ConfigClientOptions &,
                    int);
    FACTORY_TYPE_N2(ConfigFactory, ConfigK8sPartition,
                    ConfigK8sClient *, size_t);
    FACTORY_TYPE_N4(ConfigFactory, K8sClient,
                    const std::vector<K8sUrl> &,
                    const std::string &,
                    size_t,
                    size_t);
};

#endif  // __CONFIG__CONFIG_FACTORY_H__
