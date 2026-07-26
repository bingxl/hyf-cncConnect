#pragma once
#include <mutex>
#include <string>
#include <expected>
#include "cnc_ops.h"
#include "CncData.hpp"

class CncConnection {
public:
    static CncConnection& instance() {
        static CncConnection s;
        return s;
    }

    static std::expected<MachineData, std::string> fetch(std::string_view ip, int port) {
        CncMachineData raw{};
        std::string ip_str(ip);
        if (::fetch_machine_data(ip_str.c_str(), port, &raw) != 0) {
            return std::unexpected(std::string(raw.error_msg));
        }
        return MachineData::from_c(raw);
    }

    CncConnection(const CncConnection&) = delete;
    CncConnection& operator=(const CncConnection&) = delete;

private:
    CncConnection() = default;
    ~CncConnection() = default;
};
