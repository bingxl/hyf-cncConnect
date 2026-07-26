#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include "db_ops.h"
#include "CncData.hpp"

class Database {
public:
    static Database& instance() {
        static Database s;
        return s;
    }

    bool open(std::string_view path) {
        std::lock_guard lock(mtx_);
        db_ = ::db_open(path.data());
        if (!db_) return false;
        return ::db_init_tables(db_) == 0;
    }

    void close() {
        std::lock_guard lock(mtx_);
        if (db_) {
            ::db_close(db_);
            db_ = nullptr;
        }
    }

    bool is_open() const { return db_ != nullptr; }

    std::vector<MachineInfo> get_machines() {
        std::lock_guard lock(mtx_);
        MachineRecord records[DB_MAX_MACHINES];
        int count = ::db_get_machines(db_, records, DB_MAX_MACHINES);
        std::vector<MachineInfo> result;
        result.reserve(count);
        for (int i = 0; i < count; i++)
            result.push_back(MachineInfo::from_record(records[i]));
        return result;
    }

    std::optional<MachineInfo> get_machine(int id) {
        std::lock_guard lock(mtx_);
        MachineRecord rec;
        if (::db_get_machine_by_id(db_, id, &rec) != 0) return std::nullopt;
        return MachineInfo::from_record(rec);
    }

    bool add_machine(std::string_view name, std::string_view ip, int port) {
        std::lock_guard lock(mtx_);
        return ::db_add_machine(db_, name.data(), ip.data(), port) == 0;
    }

    bool update_machine(int id, std::string_view name, std::string_view ip, int port) {
        std::lock_guard lock(mtx_);
        return ::db_update_machine(db_, id, name.data(), ip.data(), port) == 0;
    }

    bool delete_machine(int id) {
        std::lock_guard lock(mtx_);
        return ::db_delete_machine(db_, id) == 0;
    }

    int save_batch(const std::vector<HistoryEntry>& entries) {
        std::lock_guard lock(mtx_);
        if (entries.empty()) return -1;
        std::vector<HistoryRecord> records(entries.size());
        for (size_t i = 0; i < entries.size(); i++) {
            records[i].machine_id = entries[i].machine_id;
            records[i].required = entries[i].required;
            records[i].current = entries[i].current;
            records[i].total = entries[i].total;
        }
        return ::db_save_batch(db_, records.data(), static_cast<int>(records.size()));
    }

    std::vector<BatchInfoCpp> get_batches(int max_count = 4) {
        std::lock_guard lock(mtx_);
        BatchInfo records[4];
        int count = ::db_get_batch_list(db_, records, max_count);
        std::vector<BatchInfoCpp> result;
        result.reserve(count);
        for (int i = 0; i < count; i++)
            result.push_back({records[i].batch_id, records[i].save_time});
        return result;
    }

    std::vector<HistoryEntry> get_batch_history(int batch_id) {
        std::lock_guard lock(mtx_);
        HistoryRecord records[DB_MAX_MACHINES];
        int count = ::db_get_batch_history(db_, batch_id, records, DB_MAX_MACHINES);
        std::vector<HistoryEntry> result;
        result.reserve(count);
        for (int i = 0; i < count; i++) {
            HistoryEntry e;
            e.id = records[i].id;
            e.machine_id = records[i].machine_id;
            e.required = records[i].required;
            e.current = records[i].current;
            e.total = records[i].total;
            e.ok = true;
            result.push_back(e);
        }
        return result;
    }

    bool delete_batch(int batch_id) {
        std::lock_guard lock(mtx_);
        return ::db_delete_batch(db_, batch_id) == 0;
    }

    bool delete_history_entry(int id) {
        std::lock_guard lock(mtx_);
        return ::db_delete_history_entry(db_, id) == 0;
    }

    bool update_history_entry(int id, long required, long current, long total) {
        std::lock_guard lock(mtx_);
        return ::db_update_history_entry(db_, id, required, current, total) == 0;
    }

    int batch_count() {
        std::lock_guard lock(mtx_);
        return ::db_get_batch_count(db_);
    }

    std::vector<BatchInfoCpp> get_batches_paged(int page, int page_size = 10) {
        std::lock_guard lock(mtx_);
        int offset = page * page_size;
        BatchInfo records[10];
        int count = ::db_get_batches_paged(db_, offset, page_size, records);
        std::vector<BatchInfoCpp> result;
        result.reserve(count);
        for (int i = 0; i < count; i++)
            result.push_back({records[i].batch_id, records[i].save_time});
        return result;
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    int schema_version() {
        std::lock_guard lock(mtx_);
        return ::db_get_schema_version(db_);
    }

    void set_schema_version(int v) {
        std::lock_guard lock(mtx_);
        ::db_set_schema_version(db_, v);
    }

private:
    Database() = default;
    ~Database() { close(); }

    DbHandle db_ = nullptr;
    mutable std::mutex mtx_;
};
