#include <algorithm>
#include "OverviewVm.hpp"
#include "core/Database.hpp"
#include "core/CncConnection.hpp"

void OverviewVm::refresh() {
    auto machines = Database::instance().get_machines();
    std::sort(machines.begin(), machines.end(),
              [](const MachineInfo& a, const MachineInfo& b) {
                  return a.name < b.name;
              });

    std::vector<OverviewItem> placeholders;
    placeholders.reserve(machines.size());
    for (const auto& m : machines) {
        OverviewItem item;
        item.machine_id = m.id;
        item.name = m.name;
        item.loading = true;
        placeholders.push_back(std::move(item));
    }

    streaming_fetch_update(machines, data, std::move(placeholders),
        [](const MachineInfo& m) {
            OverviewItem item;
            item.machine_id = m.id;
            item.name = m.name;
            auto result = CncConnection::fetch(m.ip, m.port);
            if (result) {
                item.current = result->part_count.current;
                item.required = result->part_count.required;
                item.total = result->part_count.total;
                item.ok = result->ok;
                item.alarm = result->status.alarm != 0;
            }
            item.loading = false;
            return item;
        });
}
