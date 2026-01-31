#include "TelemetryLogger.h"

void TelemetryLogger::loop() {
    int batch_count = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    while (running) {
        auto start_time = std::chrono::steady_clock::now();
        sample();
        if (++batch_count >= 50) {
            sqlite3_exec(db, "COMMIT; BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
            batch_count = 0;
        }
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (elapsed < std::chrono::milliseconds(100)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100) - elapsed);
        }
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}
