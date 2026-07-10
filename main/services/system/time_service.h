#pragma once
#include "core/service.h"

class TimeService : public Service {
public:
    TimeService(EventBus& bus);
    esp_err_t init() override;
    void deinit() override;
    bool is_synced() const { return timeSynced_; }

private:
    void sync_time();
    int evtHandle_ = 0;
    bool timeSynced_ = false;
};
