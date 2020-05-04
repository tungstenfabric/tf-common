/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>

#include "config_factory.h"

template <>
ConfigFactory *Factory<ConfigFactory>::singleton_ = NULL;

#include "config_cassandra_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigCassandraPartition,
                        ConfigCassandraPartition);

#include "config_cassandra_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigCassandraClient,
                        ConfigCassandraClient);

#include "config_amqp_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigAmqpChannel, ConfigAmqpChannel);

#ifdef CONTRAIL_ETCD_INCL
#include "config_etcd_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigEtcdPartition,
                        ConfigEtcdPartition);
#include "config_etcd_client.h"
FACTORY_STATIC_REGISTER(ConfigFactory, ConfigEtcdClient,
                        ConfigEtcdClient);
#include "database/etcd/eql_if.h"
FACTORY_STATIC_REGISTER(ConfigFactory, EtcdIf, EtcdIf);
#endif

#include "database/cassandra/cql/cql_if.h"
FACTORY_STATIC_REGISTER(ConfigFactory, CqlIf, CqlIf);
