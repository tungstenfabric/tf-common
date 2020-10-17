/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __K8S_CLIENT_LOG_H__
#define __K8S_CLIENT_LOG_H__

#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"

#define K8S_CLIENT_TRACE_BUF "K8sClientTraceBuf"
extern SandeshTraceBufferPtr K8sClientTraceBuf;

// Log and trace regular messages

#define K8S_CLIENT_DEBUG_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (false)

#define K8S_CLIENT_DEBUG(obj, arg) \
do { \
    if (LoggingDisabled()) break; \
    std::ostringstream _os; \
    _os << arg; \
    K8S_CLIENT_DEBUG_LOG(obj, Category::K8S_CLIENT, _os.str()); \
    K8S_CLIENT_TRACE(obj##Trace, _os.str()); \
} while (false)


#define K8S_CLIENT_TRACE(obj, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::TraceMsg(K8sClientTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
    } \
} while (false)

#define K8S_CLIENT_DEBUG_ONLY(obj, ...) \
do { \
    K8S_CLIENT_DEBUG_LOG(obj, Category::K8S_CLIENT, __VA_ARGS__); \
} while (false)

// Warnings

#define K8S_CLIENT_WARN_LOG(obj, category, ...) \
do { \
    if (!LoggingDisabled()) { \
        obj::Send(g_vns_constants.CategoryNames.find(category)->second, \
                  SandeshLevel::SYS_WARN, __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (false)

#define K8S_CLIENT_WARN(obj, ...) \
do { \
    K8S_CLIENT_WARN_LOG(obj, Category::K8S_CLIENT, __VA_ARGS__); \
    K8S_CLIENT_TRACE(obj##Trace, __VA_ARGS__); \
} while (false)

#endif  // __K8S_CLIENT_LOG_H__
