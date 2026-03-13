#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include "utils.hpp"

#define LOG_FILE "logs/traffic_sim.log"

void log_event(const char* msg) {
    FILE* fp = fopen(LOG_FILE, "a");
    if (fp == NULL) {
        perror("Failed to open log file");
        return;
    }

    time_t now;
    time(&now);
    char* date = ctime(&now);
    date[strlen(date) - 1] = '\0'; //Remove newline

    fprintf(fp, "[%s] %s\n", date, msg);
    //Print to console only if QUIET environment variable is not set
    if (getenv("QUIET") == NULL) {
        printf("[%s] %s\n", date, msg);
    }

    fclose(fp);
}

double get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
