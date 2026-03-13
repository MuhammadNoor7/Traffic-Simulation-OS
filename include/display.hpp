#ifndef DISPLAY_HPP
#define DISPLAY_HPP
#include <atomic>
#include <string>

struct VisualizationConfig {
    std::string log_file;
    unsigned int window_width;
    unsigned int window_height;
    unsigned int target_fps;
    float log_poll_interval_sec;
    unsigned int expected_vehicles; //optional: number of vehicles simulator will generate
};

void run_visualization_loop(VisualizationConfig config);
void request_visualization_shutdown();
bool visualization_user_exit_requested();
#endif
