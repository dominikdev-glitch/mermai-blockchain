#include "mermai/mermai_pubsub.hpp"
#include <sstream>
#include <iomanip>

namespace mermai {

std::string mermai_pubsub_engine::subscribe(const std::string& topic, const std::string& filter) {
    std::lock_guard<std::mutex> lock(sub_mutex);
    std::ostringstream ss;
    ss << "0x" << std::hex << next_sub_id++;
    std::string sub_id = ss.str();

    rpc_subscription sub;
    sub.id = sub_id;
    sub.topic = topic;
    sub.filter = filter;
    subscriptions[sub_id] = sub;
    return sub_id;
}

bool mermai_pubsub_engine::unsubscribe(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(sub_mutex);
    return subscriptions.erase(sub_id) > 0;
}

void mermai_pubsub_engine::publish(const std::string& topic, const std::string& event_json) {
    std::lock_guard<std::mutex> lock(sub_mutex);
    for (auto& kv : subscriptions) {
        if (kv.second.topic == topic) {
            kv.second.pending_events.push_back(event_json);
        }
    }
}

std::vector<std::string> mermai_pubsub_engine::poll_events(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(sub_mutex);
    auto it = subscriptions.find(sub_id);
    if (it == subscriptions.end()) return {};

    std::vector<std::string> events = std::move(it->second.pending_events);
    it->second.pending_events.clear();
    return events;
}

size_t mermai_pubsub_engine::get_subscriber_count(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(sub_mutex);
    if (topic.empty()) return subscriptions.size();
    size_t count = 0;
    for (const auto& kv : subscriptions) {
        if (kv.second.topic == topic) count++;
    }
    return count;
}

void mermai_pubsub_engine::clear() {
    std::lock_guard<std::mutex> lock(sub_mutex);
    subscriptions.clear();
}

} // namespace mermai
