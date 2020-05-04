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

namespace etcd { namespace etcdql { class EtcdIf; } }
using etcd::etcdql::EtcdIf;

class ConfigAmqpChannel;
class ConfigCassandraClient;
class ConfigCassandraPartition;
class ConfigEtcdClient;
class ConfigEtcdPartition;
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
    FACTORY_TYPE_N4(ConfigFactory, ConfigEtcdClient, ConfigClientManager *,
                    EventManager *, const ConfigClientOptions &,
                    int);
    FACTORY_TYPE_N2(ConfigFactory, ConfigEtcdPartition,
                    ConfigEtcdClient *, size_t);
    FACTORY_TYPE_N3(ConfigFactory, EtcdIf,
                    const std::vector<std::string> &,
                    const int, bool);
};

#endif  // __CONFIG__CONFIG_FACTORY_H__
