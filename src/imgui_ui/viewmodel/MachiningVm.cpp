#include "MachiningVm.hpp"
#include "core/Database.hpp"
#include <ctime>
#include <cstdio>
#include <cmath>
#include <filesystem>

namespace {

long add_local(long t, int days) {
    time_t tt = t;
    tm lt{};
    localtime_s(&lt, &tt);
    lt.tm_mday += days;
    return (long)mktime(&lt);
}

} // namespace

void MachiningVm::load_machines() {
    machines = Database::instance().get_machines();
    if (machines.empty())
        return;
    if (selected_machine_index >= (int)machines.size())
        selected_machine_index = 0;
    machine_id = machines[selected_machine_index].id;
    machine_name = machines[selected_machine_index].name;
    range_valid = false;
    loaded = false;
}

void MachiningVm::apply_quick_range(const char *kind) {
    time_t now = time(nullptr);
    long today_start, today_end;

    if (std::string(kind) == "today") {
        // today 00:00 -> tomorrow 00:00
        tm lt{};
        localtime_s(&lt, &now);
        lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
        today_start = (long)mktime(&lt);
        today_end = add_local(today_start, 1);
        t_begin = today_start;
        t_end = today_end;
        range_valid = true;
        refresh();
        return;
    }
    if (std::string(kind) == "yesterday") {
        tm lt{};
        localtime_s(&lt, &now);
        lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
        today_start = (long)mktime(&lt);
        t_begin = add_local(today_start, -1);
        t_end = today_start;
        range_valid = true;
        refresh();
        return;
    }
    if (std::string(kind) == "shift_day") {   // 08:30 ~ 20:30 same day
        tm lt{};
        localtime_s(&lt, &now);
        lt.tm_hour = 8; lt.tm_min = 30; lt.tm_sec = 0;
        t_begin = (long)mktime(&lt);
        lt.tm_hour = 20; lt.tm_min = 30;
        t_end = (long)mktime(&lt);
        range_valid = true;
        refresh();
        return;
    }
    if (std::string(kind) == "shift_night") { // 20:30 ~ next day 08:30
        tm lt{};
        localtime_s(&lt, &now);
        int hour = lt.tm_hour;
        int minute = lt.tm_min;
        tm base{};
        localtime_s(&base, &now);
        base.tm_hour = 20; base.tm_min = 30; base.tm_sec = 0;
        t_begin = (long)mktime(&base);
        tm end{};
        localtime_s(&end, &now);
        end.tm_hour = 8; end.tm_min = 30; end.tm_sec = 0;
        t_end = (long)mktime(&end);
        if (hour < 8 || (hour == 8 && minute < 30)) {
            // morning: the relevant night shift is yesterday 20:30 -> today 8:30
            t_begin = add_local(t_begin, -1);
        } else {
            // otherwise today 20:30 -> tomorrow 8:30
            t_end = add_local(t_end, 1);
        }
        range_valid = true;
        refresh();
        return;
    }
    range_valid = false;
}

void MachiningVm::refresh() {
    buckets.clear();
    products.clear();
    total_machining_secs = 0;
    total_produced = 0;
    loaded = false;
    message.clear();

    if (machine_id <= 0 || !range_valid) {
        message = "请选择机床和时间范围";
        return;
    }
    if (t_end <= t_begin) {
        message = "结束时间必须晚于开始时间";
        return;
    }

    auto b = Database::instance().get_buckets(machine_id, t_begin, t_end);
    auto p = Database::instance().get_products(machine_id, t_begin, t_end);

    // use the bucket/local sample-interval to convert machining count -> seconds.
    // We don't know the exact interval here; approximate 1 sample ~ 5s default.
    // Better: compute from study range width and sample density (sample_count*5s).
    const long approx_interval = 5; // default sample interval (config default)
    for (auto& x : b) {
        BucketRow r;
        r.ts_bucket = x.ts_bucket;
        r.machining_secs = (long)x.machining_count * approx_interval;
        r.sample_count = x.sample_count;
        r.produced = x.produced;
        buckets.push_back(r);
        total_machining_secs += r.machining_secs;
        total_produced += r.produced;
    }

    for (auto& x : p) {
        ProductRow r;
        r.comment = x.program_comment[0] ? x.program_comment : "(未识别)";
        r.machining_secs = (long)x.machining_count * approx_interval;
        r.produced = x.produced;
        products.push_back(r);
    }
    if (total_machining_secs > 0) {
        for (auto& r : products)
            r.share = (double)r.machining_secs / (double)total_machining_secs;
    } else {
        for (auto& r : products)
            r.share = 0.0;
    }

    loaded = true;
}

const char* MachiningVm::default_export_path() const {
    static std::string p;
    p = std::getenv("USERPROFILE");
    p += "\\data-collect\\machining_report.csv";
    return p.c_str();
}

bool MachiningVm::export_csv(const std::string &path) const {
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return false;

    fprintf(f, "机床,产品编码,加工时长(秒),加工时长,加工数量,占比\n");
    for (auto& r : products) {
        char buf[64];
        int h = (int)(r.machining_secs / 3600);
        int m = (int)((r.machining_secs % 3600) / 60);
        int s = (int)(r.machining_secs % 60);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        fprintf(f, "%s,\"%s\",%ld,%s,%ld,%.1f%%\n",
                machine_name.c_str(), r.comment.c_str(),
                r.machining_secs, buf, r.produced, r.share * 100.0);
    }
    // bucket summary section
    fprintf(f, "\n时间区间(半小时),加工样本数,加工时长(秒),加工数量\n");
    for (auto& r : buckets) {
        char tbuf[32];
        time_t tt = (time_t)r.ts_bucket;
        tm lt{};
        localtime_s(&lt, &tt);
        strftime(tbuf, sizeof(tbuf), "%m-%d %H:%M", &lt);
        fprintf(f, "%s,%d,%ld,%ld\n", tbuf, r.sample_count,
                r.machining_secs, r.produced);
    }
    fprintf(f, "\n合计,,\n");
    fclose(f);
    return true;
}
