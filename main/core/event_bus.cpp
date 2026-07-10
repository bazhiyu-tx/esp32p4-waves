#include "event_bus.h"
#include <algorithm>

EventBus::EventBus() {
    asyncQueue_ = xQueueCreate(16, sizeof(EventId));
}

EventBus::~EventBus() {
    if (asyncQueue_) vQueueDelete(asyncQueue_);
}

int EventBus::on(EventId evt, Callback cb) {
    int h = nextHandle_++;
    subs_[evt].push_back({h, std::move(cb)});
    return h;
}

void EventBus::off(int handle) {
    for (auto& [evt, entries] : subs_) {
        auto it = std::remove_if(entries.begin(), entries.end(),
            [handle](const Entry& e) { return e.id == handle; });
        entries.erase(it, entries.end());
    }
}

void EventBus::post(EventId evt, void* data) {
    auto it = subs_.find(evt);
    if (it != subs_.end()) {
        for (auto& e : it->second) {
            if (e.cb) e.cb(data);
        }
    }
}

void EventBus::postAsync(EventId evt, void* data) {
    (void)data;
    if (asyncQueue_) {
        xQueueSendFromISR(asyncQueue_, &evt, nullptr);
    }
}

void EventBus::dispatch() {
    EventId evt;
    while (xQueueReceive(asyncQueue_, &evt, 0) == pdTRUE) {
        post(evt);
    }
}
