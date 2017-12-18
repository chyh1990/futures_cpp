#pragma once

#include <ostream>
#include <cinttypes>
#include <vector>
#include <string>

namespace futures {
namespace zookeeper {

// XXX copy from ZooKeeper.jute.h
struct NodeState {
    int64_t czxid;
    int64_t mzxid;
    int64_t ctime;
    int64_t mtime;
    int32_t version;
    int32_t cversion;
    int32_t aversion;
    int64_t ephemeralOwner;
    int32_t dataLength;
    int32_t numChildren;
    int64_t pzxid;
};

inline std::ostream& operator <<(std::ostream& os, const NodeState& n) {
#define __print_f(f) os << #f "=" << n.f << '\n'
    __print_f(czxid);
    __print_f(mzxid);
    __print_f(ctime);
    __print_f(mtime);
    __print_f(version);
    __print_f(cversion);
    __print_f(aversion);
    __print_f(ephemeralOwner);
    __print_f(dataLength);
    __print_f(numChildren);
    __print_f(pzxid);
#undef __print_f
    return os;
}

using StringList = std::vector<std::string>;

using GetChildrenResult = StringList;
using GetChildren2Result = std::tuple<StringList, NodeState>;
using GetResult = std::string;

enum class EventType : int {
    Unknown,
    Created,
    Deleted,
    Changed,
    Child,
    Session,
    NotWatching,
};

inline std::ostream& operator <<(std::ostream& os, const EventType& ev) {
    switch (ev) {
        case EventType::Created:
            os << "Created";
            break;
        case EventType::Deleted:
            os << "Deleted";
            break;
        case EventType::Changed:
            os << "Changed";
            break;
        case EventType::Child:
            os << "Child";
            break;
        case EventType::Session:
            os << "Session";
            break;
        case EventType::NotWatching:
            os << "NotWatching";
            break;
        case EventType::Unknown:
        default:
            os << "Unknown";
    }
    return os;
}

struct WatchedEvent {
    EventType type;
    int state;
    std::string path;
};

}
}
