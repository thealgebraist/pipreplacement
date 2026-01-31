#include "TelemetryLogger.h"

TelemetryLogger::~TelemetryLogger() {
    stop();
    if (insert_stmt) sqlite3_finalize(insert_stmt);
    if (db) sqlite3_close(db);
}

void TelemetryLogger::start() {
    if (running) return;
    running = true;
    worker = std::thread(&TelemetryLogger::loop, this);
}

void TelemetryLogger::stop() {
    if (!running) return;
    running = false;
    if (worker.joinable()) worker.join();
}
