#pragma once

#include <vector>
#include <memory>
#include "service.h"
#include "event_bus.h"

/**
 * Manages all services: creation, ordered init, deinit.
 *
 * Usage:
 *   ServiceManager svc;
 *   svc.create<FooService>();
 *   svc.create<BarService>();
 *   svc.start();   // inits all, then event loop
 */
class ServiceManager {
public:
    ServiceManager() = default;
    ~ServiceManager() { stop(); }

    /// Register a service. Created but not yet initialized.
    template<typename T, typename... Args>
    T& create(Args&&... args) {
        auto svc = std::make_unique<T>(bus_, std::forward<Args>(args)...);
        auto* ptr = svc.get();
        services_.push_back(std::move(svc));
        return *ptr;
    }

    /// Init all services in registration order, then post EV_SYS_STARTUP.
    esp_err_t start() {
        for (auto& svc : services_) {
            ESP_LOGI("SvcMgr", "Starting %s...", svc->name());
            esp_err_t e = svc->init();
            if (e != ESP_OK) {
                ESP_LOGE("SvcMgr", "  %s init failed (0x%x)", svc->name(), e);
                return e;
            }
            ESP_LOGI("SvcMgr", "  %s OK", svc->name());
        }
        bus_.post(EV_SYS_STARTUP);
        return ESP_OK;
    }

    /// Deinit all in reverse order.
    void stop() {
        for (auto it = services_.rbegin(); it != services_.rend(); ++it) {
            (*it)->deinit();
        }
        services_.clear();
    }

    /// Dispatch pending async events (call in main loop).
    void dispatch() { bus_.dispatch(); }

    /// Access the event bus.
    EventBus& bus() { return bus_; }

private:
    EventBus bus_;
    std::vector<std::unique_ptr<Service>> services_;
};
