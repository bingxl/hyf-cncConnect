#pragma once
#include <string>
#include <vector>
#include "core/CncData.hpp"

struct BucketRow {
    long ts_bucket = 0;      // aligned 30-min start (epoch sec)
    long machining_secs = 0; // machining samples in bucket
    int  sample_count = 0;
    long produced = 0;       // part delta in bucket
};

struct ProductRow {
    std::string comment;
    long machining_secs = 0;
    long produced = 0;
    double share = 0.0;      // 0..1 of total machining time
};

class MachiningVm {
public:
    // time range (epoch seconds), aligned to 30min in local time
    long t_begin = 0;
    long t_end = 0;
    bool range_valid = false;

    // selected machine
    int machine_id = 0;
    std::string machine_name;

    std::vector<MachineInfo> machines;   // machine list for the combo
    int selected_machine_index = 0;

    std::vector<BucketRow> buckets;
    std::vector<ProductRow> products;
    long total_machining_secs = 0;
    long total_produced = 0;

    bool loaded = false;
    std::string message;

    void load_machines();
    void refresh();

    // clear selection / set defaults
    void apply_quick_range(const char *kind); // "shift_day" | "shift_night" | "today" | "yesterday"

    bool export_csv(const std::string &path) const;
    const char* default_export_path() const;
};
