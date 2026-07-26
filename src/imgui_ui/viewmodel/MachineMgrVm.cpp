#include "MachineMgrVm.hpp"
#include "core/Database.hpp"

void MachineMgrVm::load() {
    machines = Database::instance().get_machines();
}

bool MachineMgrVm::add(std::string_view name, std::string_view ip, int port) {
    bool ok = Database::instance().add_machine(name, ip, port);
    if (ok) load();
    return ok;
}

bool MachineMgrVm::update(int id, std::string_view name, std::string_view ip, int port) {
    bool ok = Database::instance().update_machine(id, name, ip, port);
    if (ok) load();
    return ok;
}

bool MachineMgrVm::remove(int id) {
    bool ok = Database::instance().delete_machine(id);
    if (ok) load();
    return ok;
}
