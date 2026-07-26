#include "MachineDetailVm.hpp"
#include "core/Database.hpp"
#include "core/CncConnection.hpp"

void MachineDetailVm::fetch(int machine_id) {
    auto machine = Database::instance().get_machine(machine_id);
    if (!machine) {
        current_name.clear();
        return;
    }
    current_name = machine->name;
    std::string ip = machine->ip;
    int port = machine->port;
    data.start([ip = std::move(ip), port]() {
        auto result = CncConnection::fetch(ip, port);
        if (result) return std::move(*result);
        MachineData md;
        md.ok = false;
        md.error_msg = result.error();
        return md;
    });
}
