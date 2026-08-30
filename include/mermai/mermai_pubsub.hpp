#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <memory>

namespace mermai {

struct rpc_subscription {
    std::string id;
    std::string topic;
    std::string filter;
    std::vector<std::string> pending_events;
};

class mermai_pubsub_engine {
private:
    mutable std::mutex sub_mutex;
    std::map<std::string, rpc_subscription> subscriptions;
    uint64_t next_sub_id = 1;

public:
    mermai_pubsub_engine() = default;

    std::string subscribe(const std::string& topic, const std::string& filter = "");
    bool unsubscribe(const std::string& sub_id);
    void publish(const std::string& topic, const std::string& event_json);
    std::vector<std::string> poll_events(const std::string& sub_id);
    size_t get_subscriber_count(const std::string& topic = "") const;
    void clear();
};

} // namespace mermai
