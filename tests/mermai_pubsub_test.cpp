#include "mermai/mermai_pubsub.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Starting Mermai Phase 6 - WebSocket PubSub Subscription tests..." << std::endl;

    mermai::mermai_pubsub_engine pubsub;

    std::string sub_heads_1 = pubsub.subscribe("newHeads");
    std::string sub_heads_2 = pubsub.subscribe("newHeads");
    std::string sub_txs = pubsub.subscribe("newPendingTransactions");

    assert(!sub_heads_1.empty() && !sub_heads_2.empty() && !sub_txs.empty());
    assert(sub_heads_1 != sub_heads_2);
    assert(pubsub.get_subscriber_count("newHeads") == 2);
    assert(pubsub.get_subscriber_count("newPendingTransactions") == 1);
    std::cout << "  [OK] Subscriptions created: 2 for newHeads, 1 for newPendingTransactions" << std::endl;

    std::string block_event = "{\"height\":10,\"hash\":\"0xabc123\"}";
    pubsub.publish("newHeads", block_event);

    auto events1 = pubsub.poll_events(sub_heads_1);
    auto events2 = pubsub.poll_events(sub_heads_2);
    auto events_tx = pubsub.poll_events(sub_txs);

    assert(events1.size() == 1 && events1[0] == block_event);
    assert(events2.size() == 1 && events2[0] == block_event);
    assert(events_tx.empty());
    std::cout << "  [OK] Topic event isolation verified" << std::endl;

    auto events1_repeat = pubsub.poll_events(sub_heads_1);
    assert(events1_repeat.empty());
    std::cout << "  [OK] Event queue consumed upon polling" << std::endl;

    assert(pubsub.unsubscribe(sub_heads_1));
    assert(pubsub.get_subscriber_count("newHeads") == 1);
    assert(!pubsub.unsubscribe("invalid_id"));
    std::cout << "  [OK] Unsubscription verified" << std::endl;

    std::cout << "All PubSub Subscription tests passed successfully!" << std::endl;
    return 0;
}
