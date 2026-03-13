//Standalone SFML Visualization Launcher
//Run this alongside the main traffic_sim to visualize in real-time

#include <cstdio>
#include <cstdlib>
#include "display.hpp"

int main(int argc, char* argv[]) {
    printf("Traffic Simulation Visualizer (SFML)\n");
    printf("=====================================\n");
    printf("Press ESC or close window to exit.\n\n");
    
    VisualizationConfig config;
    config.log_file = "logs/traffic_sim.log";
    config.window_width = 1024;   //Larger window
    config.window_height = 700;
    config.target_fps = 60;
    config.log_poll_interval_sec = 0.08f;  //Poll slightly faster
    config.expected_vehicles = 0;
    
    //Allow custom log file path
    if (argc > 1) {
        config.log_file = argv[1];
    }
    if (argc > 2) {
        config.expected_vehicles = (unsigned int)atoi(argv[2]);
    }
    
    printf("Monitoring log file: %s\n", config.log_file.c_str());
    printf("Start the simulation with: ./bin/traffic_sim 3\n\n");
    
    run_visualization_loop(config);
    
    printf("Visualizer closed.\n");
    return 0;
}
