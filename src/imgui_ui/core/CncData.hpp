#pragma once
#include <string>
#include <optional>
#include "cnc_ops.h"
#include "db_ops.h"

struct MachineInfo {
    int id = 0;
    std::string name;
    std::string ip;
    int port = 8193;

    static MachineInfo from_record(const MachineRecord& r) {
        return {r.id, r.name, r.ip, r.port};
    }
};

struct MachineData {
    bool ok = false;
    std::string error_msg;
    CncSystemInfo sys{};
    CncStatus status{};
    CncPositions pos{};
    CncAlarms alarms{};
    CncActData act{};
    CncProgramInfo prog{};
    CncDynamicData dyn{};
    PartCount part_count{};
    CncProgramList prog_list{};
    CncMacroData macro_vars{};
    CncToolOffsetData tool_offsets{};
    CncWorkZeroData work_zero{};
    long param_6750 = 0;
    long setting_0 = 0;
    short path_current = 0;
    short path_count = 0;

    static MachineData from_c(const CncMachineData& c) {
        MachineData d;
        d.ok = c.ok != 0;
        d.error_msg = c.error_msg;
        d.sys = c.sys;
        d.status = c.status;
        d.pos = c.pos;
        d.alarms = c.alarms;
        d.act = c.act;
        d.prog = c.prog;
        d.dyn = c.dyn;
        d.part_count = c.part_count;
        d.prog_list = c.prog_list;
        d.macro_vars = c.macro_vars;
        d.tool_offsets = c.tool_offsets;
        d.work_zero = c.work_zero;
        d.param_6750 = c.param_6750;
        d.setting_0 = c.setting_0;
        d.path_current = c.path_current;
        d.path_count = c.path_count;
        return d;
    }
};

struct OverviewItem {
    int machine_id = 0;
    std::string name;
    long current = 0;
    long required = 0;
    long total = 0;
    bool ok = false;
    bool alarm = false;
};

struct HistoryEntry {
    int id = 0;
    int machine_id = 0;
    std::string name;
    long required = 0;
    long current = 0;
    long total = 0;
    bool ok = false;
    bool alarm = false;
};

struct CalcItem {
    std::string name;
    long current = -1;
    long base = -1;
    long diff = 0;
    bool ok = false;
    std::string status;
};

struct BatchInfoCpp {
    int batch_id = 0;
    std::string save_time;
};
