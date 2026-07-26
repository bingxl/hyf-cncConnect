#include "OverviewVm.hpp"
#include "core/Database.hpp"
#include "core/CncConnection.hpp"

void OverviewVm::refresh() {
    auto machines = Database::instance().get_machines();
    streaming_fetch(machines, data, [](const MachineInfo& m) {
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
        } else {
            item.ok = false;
        }
        return item;
    });
}
