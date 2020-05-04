/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "base/connection_info.h"

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/foreach.hpp>

#include "base/string_util.h"
#include "base/sandesh/process_info_constants.h"

namespace process {

using std::vector;
using std::string;

// ConnectionState
boost::scoped_ptr<ConnectionState> ConnectionState::instance_;
boost::scoped_ptr<ConnectionStateManager> ConnectionStateManager::instance_;


ConnectionState::ConnectionState(SendUveCb send_uve_cb) :
    send_uve_cb_(send_uve_cb) {
}

// CreateInstance should be called from ConnectionStateManager::Init()
void ConnectionState::CreateInstance(SendUveCb send_uve_cb) {
    // The assert is to catch errors where ConnectionState::GetInstance()
    // is called before ConnectionStateManager::Init()
    assert(instance_ == NULL);
    instance_.reset(new ConnectionState(send_uve_cb));
}

ConnectionState* ConnectionState::GetInstance() {
    if (instance_ == NULL) {
        // This is needed to handle unit tests that do not call
        // ConnectionStateManager::Init()
        instance_.reset(new ConnectionState(NULL));
    }
    return instance_.get();
}

void ConnectionState::Update() {
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

void ConnectionState::UpdateInternal(ConnectionType::type ctype,
    const string &name, ConnectionStatus::type status,
    const vector<Endpoint> &servers, string message) {
    // Populate key
    ConnectionInfoKey key(ctype, name);
    // Populate info
    ConnectionInfo info;
    info.set_type(
        g_process_info_constants.ConnectionTypeNames.find(ctype)->second);
    info.set_name(name);
    vector<string> server_addrs;
    BOOST_FOREACH(const Endpoint &server, servers) {
        boost::system::error_code ec;
        string saddr(server.address().to_string(ec));
        int sport(server.port());
        string server_address(saddr + ":" + integerToString(sport));
        server_addrs.push_back(server_address);
    }
    info.set_server_addrs(server_addrs);
    info.set_status(
        g_process_info_constants.ConnectionStatusNames.find(status)->second);
    info.set_description(message);
    // Lookup connection info map
    tbb::mutex::scoped_lock lock(mutex_);
    ConnectionInfoMap::iterator it = connection_map_.find(key);
    if (it != connection_map_.end()) {
        // Update
        if (it->second.server_addrs == info.server_addrs &&
            it->second.status == info.status &&
            it->second.description == info.description) {
            // Do not send UVE if there is no change in the server_addrs,
            // status or description
            return;
        }
        it->second = info;
    } else {
        // Add
        connection_map_[key] = info;
    }
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

void ConnectionState::Update(ConnectionType::type ctype,
    const string &name, ConnectionStatus::type status,
    const vector<Endpoint> &servers, string message) {
    UpdateInternal(ctype, name, status, servers, message);
}

void ConnectionState::Update(ConnectionType::type ctype,
    const string &name, ConnectionStatus::type status,
    Endpoint server, string message) {
    UpdateInternal(ctype, name, status, boost::assign::list_of(server),
        message);
}

void ConnectionState::Delete(ConnectionType::type ctype,
    const string &name) {
    // Construct key
    ConnectionInfoKey key(ctype, name);
    // Delete
    tbb::mutex::scoped_lock lock(mutex_);
    connection_map_.erase(key);
    if (!send_uve_cb_.empty()) {
        send_uve_cb_();
    }
}

vector<ConnectionInfo> ConnectionState::GetInfosUnlocked() const {
    vector<ConnectionInfo> infos;
    for (ConnectionInfoMap::const_iterator it = connection_map_.begin();
         it != connection_map_.end(); it++) {
        infos.push_back(it->second);
    }
    return infos;
}

vector<ConnectionInfo> ConnectionState::GetInfos() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return GetInfosUnlocked();
}

void GetProcessStateCb(const vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, string &message,
    const vector<ConnectionTypeName> &expected_connections) {
    // Determine if the number of connections is as expected.
    size_t num_connections(cinfos.size());
    if (num_connections != expected_connections.size()) {
        GetConnectionInfoMessage(cinfos, expected_connections, message);
        state = ProcessState::NON_FUNCTIONAL;
        return;
    }
    string cup(g_process_info_constants.ConnectionStatusNames.
                    find(ConnectionStatus::UP)->second);
    bool is_cup = true;
    // Iterate to determine process connectivity status
    for (vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const string &conn_status(cinfo.get_status());
        if (conn_status != cup) {
            is_cup = false;
            if (message.empty()) {
                message = cinfo.get_type();
            } else {
                message += ", " + cinfo.get_type();
            }
            const string &name(cinfo.get_name());
            if (!name.empty()) {
                message += ":" + name;
            }
        }
    }
    // All critical connections are in good condition.
    if (is_cup) {
        state = ProcessState::FUNCTIONAL;
    } else {
        state = ProcessState::NON_FUNCTIONAL;
        message += " connection down";
    }
    return;
}

// Custom find function to compare ConnectionInfo and
// expected connection structures
struct CompareConnections : public std::unary_function<ConnectionTypeName,
    bool> {
    ConnectionTypeName expected_connection_;
    explicit CompareConnections(const ConnectionTypeName exp_connection) :
        expected_connection_(exp_connection) {}
    bool operator() (const ConnectionInfo cinfo) {
        if (expected_connection_.first == cinfo.get_type() &&
            expected_connection_.second == cinfo.get_name()) {
            return true;
        } else {
            return false;
        }
    }
};

void GetConnectionInfoMessage(const vector<ConnectionInfo> &cinfos,
    const vector<ConnectionTypeName> &expected_connections,
    string &message) {
    size_t num_connections(cinfos.size());
    message = "Number of connections:" + integerToString(num_connections) +
              ", Expected:" + integerToString(expected_connections.size());
    if (num_connections > expected_connections.size()) {
        size_t i = 0;
        message += " Extra: ";
        // find the extra connection
        for (vector<ConnectionInfo>::const_iterator it = cinfos.begin();
            it != cinfos.end(); it++) {
            const ConnectionInfo &cinfo(*it);
            ConnectionTypeName con_info(cinfo.get_type(), cinfo.get_name());
            vector<ConnectionTypeName>::const_iterator position;
            position = std::find(expected_connections.begin(),
                                 expected_connections.end(), con_info);
            if (position == expected_connections.end()) {
                i++;
                message += con_info.first;
                if (!con_info.second.empty()) {
                    message += ":" + con_info.second;
                }
                if (i != num_connections-expected_connections.size()) {
                    message += ",";
                }
            }
        }
    } else {
        // find the missing connection
        size_t i = 0;
        message += " Missing: ";
        for (vector<ConnectionTypeName>::const_iterator it =
             expected_connections.begin(); it != expected_connections.end();
             it++) {
            vector<ConnectionInfo>::const_iterator position;
            position = std::find_if(cinfos.begin(), cinfos.end(),
                                     CompareConnections(*it));
            // If connection is not found in cinfo, its a missing
            // connection
            if (position == cinfos.end()) {
                i++;
                message += it->first;
                if (!it->second.empty()) {
                    message += ":" + it->second;
                }
                if (i != expected_connections.size() - cinfos.size()) {
                    message += ",";
                }
            }
        }
    }
}

vector<FlagInfo>
ConnectionStateManager::GetFlagInfos(const FlagConfigVec &flag_infos) {
    FlagInfo info;
    vector<FlagInfo> infos;
    ContextInfo c_info;
    vector<ContextInfo> c_infos;
    FlagState state;
    ContextVec c_vec;
    context_iterator c_itr;
    for (flag_cfg_itr it = flag_infos.begin(); it != flag_infos.end(); it++) {
        info.set_name(it->name());
        info.set_version(it->version());
        info.set_enabled(it->enabled());
        info.set_state(state.ToString(it->state()));
        c_vec = it->context_infos();
        for (c_itr = c_vec.begin(); c_itr != c_vec.end(); c_itr++) {
            c_info.desc = c_itr->desc;
            c_info.value = c_itr->value;
            c_infos.push_back(c_info);
        }
        info.set_context_infos(c_infos);
        infos.push_back(info);
    }
    return infos;
}

} // namespace process
