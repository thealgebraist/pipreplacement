#include "spip_delta_db.h"
#include "spip_utils.h"
#include <sqlite3.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

static std::string get_delta_db_path() {
    return (fs::path(getenv("HOME")) / ".spip" / "deltas.db").string();
}

static std::string get_delta_cache_dir() {
    return (fs::path(getenv("HOME")) / ".spip" / "delta_cache").string();
}

void init_delta_db() {
    // Create directories
    fs::create_directories(get_delta_cache_dir());
    
    sqlite3* db;
    if (sqlite3_open(get_delta_db_path().c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open delta database\n";
        return;
    }
    
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS package_deltas (
            id INTEGER PRIMARY KEY,
            package_name TEXT NOT NULL,
            source_version TEXT NOT NULL,
            target_version TEXT NOT NULL,
            delta_size INTEGER NOT NULL,
            target_size INTEGER NOT NULL,
            similarity REAL NOT NULL,
            delta_path TEXT NOT NULL,
            source_url TEXT NOT NULL,
            target_url TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            UNIQUE(package_name, source_version, target_version)
        );
        CREATE INDEX IF NOT EXISTS idx_deltas_package 
            ON package_deltas(package_name, source_version);
    )";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to create delta schema: " << err_msg << "\n";
        sqlite3_free(err_msg);
    }
    
    sqlite3_close(db);
}

void store_delta(const DeltaRecord& record) {
    sqlite3* db;
    if (sqlite3_open(get_delta_db_path().c_str(), &db) != SQLITE_OK) {
        return;
    }
    
    const char* sql = R"(
        INSERT OR REPLACE INTO package_deltas 
        (package_name, source_version, target_version, delta_size, target_size, 
         similarity, delta_path, source_url, target_url, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, record.package_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, record.source_version.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, record.target_version.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, record.delta_size);
        sqlite3_bind_int64(stmt, 5, record.target_size);
        sqlite3_bind_double(stmt, 6, record.similarity);
        sqlite3_bind_text(stmt, 7, record.delta_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, record.source_url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, record.target_url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 10, record.created_at);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to store delta: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
}

std::optional<DeltaRecord> query_delta(const std::string& package,
                                       const std::string& from_version,
                                       const std::string& to_version) {
    sqlite3* db;
    if (sqlite3_open(get_delta_db_path().c_str(), &db) != SQLITE_OK) {
        return std::nullopt;
    }
    
    const char* sql = R"(
        SELECT source_version, target_version, delta_size, target_size, 
               similarity, delta_path, source_url, target_url, created_at
        FROM package_deltas
        WHERE package_name = ? AND source_version = ? AND target_version = ?
    )";
    
    sqlite3_stmt* stmt;
    std::optional<DeltaRecord> result;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, package.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, from_version.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, to_version.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            DeltaRecord rec;
            rec.package_name = package;
            rec.source_version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            rec.target_version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rec.delta_size = sqlite3_column_int64(stmt, 2);
            rec.target_size = sqlite3_column_int64(stmt, 3);
            rec.similarity = sqlite3_column_double(stmt, 4);
            rec.delta_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.source_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            rec.target_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            rec.created_at = sqlite3_column_int64(stmt, 8);
            result = rec;
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    return result;
}

bool is_delta_beneficial(const DeltaRecord& record) {
    // Only use delta if it's less than 70% of target size
    return record.delta_size < (record.target_size * 0.7);
}

void cleanup_old_deltas(size_t max_size_mb) {
    sqlite3* db;
    if (sqlite3_open(get_delta_db_path().c_str(), &db) != SQLITE_OK) {
        return;
    }
    
    // Get total size
    const char* sum_sql = "SELECT SUM(delta_size) FROM package_deltas";
    sqlite3_stmt* stmt;
    size_t total_size = 0;
    
    if (sqlite3_prepare_v2(db, sum_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total_size = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    size_t max_bytes = max_size_mb * 1024 * 1024;
    
    if (total_size > max_bytes) {
        // Delete oldest deltas
        const char* delete_sql = R"(
            DELETE FROM package_deltas 
            WHERE id IN (
                SELECT id FROM package_deltas 
                ORDER BY created_at ASC 
                LIMIT 100
            )
        )";
        
        char* err_msg = nullptr;
        sqlite3_exec(db, delete_sql, nullptr, nullptr, &err_msg);
        if (err_msg) {
            std::cerr << "Delta cleanup error: " << err_msg << "\n";
            sqlite3_free(err_msg);
        }
    }
    
    sqlite3_close(db);
}
