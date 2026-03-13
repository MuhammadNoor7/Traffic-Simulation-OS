//Phase 6: SFML-based Visualization
//This module provides a graphical view of the traffic simulation

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <SFML/Audio.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <regex>
#include <deque>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "display.hpp"

// ============================================================================
// Configuration Constants
// ============================================================================
static const float ROAD_WIDTH = 100.0f;
static const float INTERSECTION_SIZE = 140.0f;
static const float VEHICLE_SIZE = 24.0f;
static const float LANE_WIDTH = ROAD_WIDTH / 2.0f;
static const float QUEUE_SPACING = 30.0f;  //Space between queued vehicles

//Colors
static const sf::Color ROAD_COLOR(60, 60, 60);
static const sf::Color GRASS_COLOR(34, 139, 34);
static const sf::Color LINE_COLOR(255, 255, 0);
static const sf::Color PARKING_COLOR(100, 100, 100);
static const sf::Color EMERGENCY_FLASH(255, 0, 0, 100);

//Vehicle colors by type
static const sf::Color COLOR_AMBULANCE(255,0,0);      //Red
static const sf::Color COLOR_FIRETRUCK(255,100,0);    //Orange
static const sf::Color COLOR_BUS(0,100,255);          //Blue
static const sf::Color COLOR_CAR(200,200,200);        //Gray
static const sf::Color COLOR_BIKE(0,255,0);           //Green
static const sf::Color COLOR_TRACTOR(139,69,19);      //Brown

// ============================================================================
//Data Structures
// ============================================================================
enum class VehicleState {
    ARRIVING,
    WAITING,
    CROSSING,
    PARKING,
    EXITING,
    TRANSITING
};

struct VehicleInfo {
    int id;
    std::string type;
    std::string intersection;   //"F10" or "F11"
    std::string entry;          //"North","South", etc.
    std::string destination;
    VehicleState state;
    float x,y;                 //Current position for animation
    float target_x,target_y;   //Target position
    sf::Clock lifetime;
    bool is_emergency;
    int queue_slot;             //Position in queue for spreading out
    int crossing_slot;          //Position in intersection for spreading out
    std::vector<sf::Vector2f> waypoints;  // Path waypoints for road following
    int current_waypoint;
};

struct IntersectionInfo {
    float center_x, center_y;
    bool emergency_mode;
    int vehicles_crossing;
};

struct ParkingInfo {
    int parked_count;
    int total_spots;
    int waiting_count;
    int queue_capacity;
};

//Statistics tracking
struct SimulationStats {
    int total_vehicles_spawned;
    int vehicles_exited;
    int emergency_preemptions;
    int ipc_messages;
    int transit_count;  //Vehicles moving F10<->F11
};

// ============================================================================
//Global State (Thread-Safe)
// ============================================================================
static std::mutex g_state_mutex;
static std::map<std::string,VehicleInfo> g_vehicles;  //Key:"F10_1","F11_2",etc.
static IntersectionInfo g_f10_info={300.0f,350.0f,false,0};  //Larger window positions
static IntersectionInfo g_f11_info={700.0f,350.0f,false,0};
static std::map<std::string,int> g_queue_positions;  //Track queue position per direction
static int g_f10_crossing_slot=0;  //Counter for spreading vehicles in F10 intersection
static int g_f11_crossing_slot=0;  //Counter for spreading vehicles in F11 intersection
static ParkingInfo g_parking={0,10,0,5};  //10 spots,5 queue capacity
static SimulationStats g_stats={0,0,0,0,0};
static std::deque<std::string> g_log_lines;
static std::atomic<bool> g_shutdown_requested{false};
static std::atomic<bool> g_user_exit{false};
static const size_t MAX_LOG_LINES=15;

//Vehicle sprite cache (loaded on demand)
static std::map<std::string, sf::Texture> g_vehicle_textures;

//Normalize a filename/type to a simple key: lowercased,no spaces
static std::string normalize_key(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c)) && std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back((char)std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

//Preload all images from given resource directories into the texture cache.
static void preload_vehicle_textures() {
    std::vector<std::string> dirs = {"resources", "resources/icons", "assets"};
    std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".bmp"};

    for (const auto &d : dirs) {
        std::filesystem::path dirp(d);
        if (!std::filesystem::exists(dirp) || !std::filesystem::is_directory(dirp)) continue;

        for (const auto &entry : std::filesystem::directory_iterator(dirp)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;

            std::string stem = p.stem().string();
            std::string key = normalize_key(stem);
            if (key.empty()) continue;

            sf::Image img;
            if (!img.loadFromFile(p.string())) continue;
            //Make near-white pixels transparent (threshold) to remove white halos
            unsigned int w = img.getSize().x;
            unsigned int h = img.getSize().y;
            const unsigned char TH = 230; //threshold: values>=TH are considered background
            for (unsigned int yy = 0; yy < h; ++yy) {
                for (unsigned int xx = 0; xx < w; ++xx) {
                    sf::Color c = img.getPixel(xx, yy);
                    if (c.r >= TH && c.g >= TH && c.b >= TH) {
                        //make transparent
                        c.a = 0;
                        img.setPixel(xx, yy, c);
                    }
                }
            }
            sf::Texture tex;
            if (!tex.loadFromImage(img)) continue;
            g_vehicle_textures.emplace(key, std::move(tex));
            printf("[VIS] Loaded icon: %s -> key=%s\n", p.string().c_str(), key.c_str());
            fflush(stdout);
        }
    }
}

// ============================================================================
//Helper Functions
// ============================================================================
sf::Color get_vehicle_color(const std::string& type) {
    if (type=="Ambulance") return COLOR_AMBULANCE;
    if (type=="Firetruck") return COLOR_FIRETRUCK;
    if (type=="Bus") return COLOR_BUS;
    if (type=="Car") return COLOR_CAR;
    if (type=="Bike") return COLOR_BIKE;
    if (type=="Tractor") return COLOR_TRACTOR;
    return COLOR_CAR;
}

std::string get_vehicle_char(const std::string& type) {
    if (type == "Ambulance") return "A";
    if (type == "Firetruck") return "F";
    if (type == "Bus") return "B";
    if (type == "Car") return "C";
    if (type == "Bike") return "K";
    if (type == "Tractor") return "T";
    return "?";
}

// Get queue key for a direction at an intersection
std::string get_queue_key(const std::string& intersection, const std::string& direction) {
    return intersection + "_" + direction;
}

//Get position on road at intersection edge (where vehicle waits before entering)
sf::Vector2f get_intersection_edge(const IntersectionInfo& info, const std::string& dir) {
    float cx = info.center_x;
    float cy = info.center_y;
    float half = INTERSECTION_SIZE / 2.0f;
    
    if (dir=="North") return {cx+LANE_WIDTH/4,cy-half};  //Incoming lane from North
    if (dir=="South") return {cx-LANE_WIDTH/4,cy+half};  //Incoming lane from South
    if (dir=="East")  return {cx+half,cy+LANE_WIDTH/4};  //Incoming lane from East
    if (dir=="West")  return {cx-half,cy-LANE_WIDTH/4};  //Incoming lane from West
    return {cx, cy};
}

//Get exit edge position (where vehicle exits intersection)
sf::Vector2f get_exit_edge(const IntersectionInfo& info, const std::string& dir) {
    float cx = info.center_x;
    float cy = info.center_y;
    float half = INTERSECTION_SIZE / 2.0f;
    
    if (dir=="North") return {cx-LANE_WIDTH/4,cy-half};  //Outgoing lane to North
    if (dir=="South") return {cx+LANE_WIDTH/4,cy+half};  //Outgoing lane to South
    if (dir=="East")  return {cx+half,cy-LANE_WIDTH/4};  //Outgoing lane to East
    if (dir=="West")  return {cx-half,cy+LANE_WIDTH/4};  //Outgoing lane to West
    if (dir=="Parking") return {cx-half,cy+LANE_WIDTH/4}; //Exit west for parking
    return {cx, cy};
}

//Get entry position with queue offset (on road, away from intersection)
sf::Vector2f get_entry_position(const IntersectionInfo& info, const std::string& entry, int queue_pos = 0) {
    auto edge = get_intersection_edge(info, entry);
    float queue_offset = 40.0f + queue_pos * QUEUE_SPACING;
    
    if (entry == "North") return {edge.x, edge.y - queue_offset};
    if (entry == "South") return {edge.x, edge.y + queue_offset};
    if (entry == "East")  return {edge.x + queue_offset, edge.y};
    if (entry == "West")  return {edge.x - queue_offset, edge.y};
    return edge;
}

//Get final exit position (far from intersection on exit road)
sf::Vector2f get_exit_position(const IntersectionInfo& info, const std::string& dest) {
    auto edge = get_exit_edge(info, dest);
    float exit_dist = 80.0f;
    
    if (dest == "North") return {edge.x, edge.y - exit_dist};
    if (dest == "South") return {edge.x, edge.y + exit_dist};
    if (dest == "East")  return {edge.x + exit_dist, edge.y};
    if (dest == "West")  return {edge.x - exit_dist, edge.y};
    if (dest == "Parking") return {info.center_x - INTERSECTION_SIZE - 60, info.center_y + 100};
    return edge;
}

// ============================================================================
// Log Parsing
// ============================================================================
void parse_log_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    //Detect simulation restart - clear all vehicles
    if (line.find("=== Traffic Simulation Started ===") != std::string::npos) {
        g_vehicles.clear();
        g_f10_info.emergency_mode = false;
        g_f11_info.emergency_mode = false;
        g_f10_info.vehicles_crossing = 0;
        g_f11_info.vehicles_crossing = 0;
        g_parking.parked_count = 0;
        g_parking.waiting_count = 0;
        g_queue_positions.clear();
        g_stats = {0, 0, 0, 0, 0};
        g_log_lines.clear();
        printf("[VIS] Simulation restart detected - clearing state\n");
        fflush(stdout);
    }
    
    //Add to display log
    if (g_log_lines.size() >= MAX_LOG_LINES) {
        g_log_lines.pop_front();
    }
    //Extract just the message part (after timestamp)
    size_t bracket_pos = line.find("] ");
    std::string msg = (bracket_pos != std::string::npos) ? line.substr(bracket_pos + 2) : line;
    g_log_lines.push_back(msg);
    
    //Parse vehicle arrival
    std::regex arrival_regex(R"(Vehicle (\d+) \((\w+)\) Arrived at (F\d+) intersection (\w+))");
    std::smatch match;
    if (std::regex_search(line, match, arrival_regex)) {
        int id = std::stoi(match[1].str());
        std::string intersection = match[3].str();
        std::string key = intersection + "_" + std::to_string(id);
        std::string entry_dir = match[4].str();
        
    //Get queue position for this entry direction
        std::string queue_key = get_queue_key(intersection, entry_dir);
        int queue_pos = g_queue_positions[queue_key]++;
        
        VehicleInfo v;
        v.id = id;
        v.type = match[2].str();
        v.intersection = intersection;
        v.entry = entry_dir;
        v.state = VehicleState::ARRIVING;
        v.is_emergency = (v.type == "Ambulance" || v.type == "Firetruck");
        v.queue_slot = queue_pos;
        v.crossing_slot = -1;
        v.current_waypoint = 0;
        v.waypoints.clear();
        
        IntersectionInfo& info = (v.intersection == "F10") ? g_f10_info : g_f11_info;
        auto pos = get_entry_position(info, v.entry, queue_pos);
        v.x = pos.x;
        v.y = pos.y;
    //Target: move toward intersection edge on same lane (straight line on road)
        auto edge = get_intersection_edge(info, v.entry);
        v.target_x = edge.x;
        v.target_y = edge.y;
        
        g_vehicles[key] = v;
        return;
    }
    
    //Parse WAITING for emergency
    std::regex waiting_regex(R"(Vehicle (\d+).*WAITING - Emergency mode active at (F\d+))");
    if (std::regex_search(line, match, waiting_regex)) {
        std::string key = match[2].str() + "_" + match[1].str();
        if (g_vehicles.count(key)) {
            g_vehicles[key].state = VehicleState::WAITING;
        }
        return;
    }
    
    //Parse ENTERING intersection
    std::regex entering_regex(R"(Vehicle (\d+).*ENTERING intersection (F\d+))");
    if (std::regex_search(line, match, entering_regex)) {
        std::string intersection = match[2].str();
        std::string key = intersection + "_" + match[1].str();
        if (g_vehicles.count(key)) {
            g_vehicles[key].state = VehicleState::CROSSING;
            IntersectionInfo& info = (intersection == "F10") ? g_f10_info : g_f11_info;
            
            //Move vehicle to intersection edge first (snap to proper position)
            auto entry_edge = get_intersection_edge(info, g_vehicles[key].entry);
            g_vehicles[key].x = entry_edge.x;
            g_vehicles[key].y = entry_edge.y;
            
            //Target: center of intersection (will get exit target when EXITING)
            g_vehicles[key].target_x = info.center_x;
            g_vehicles[key].target_y = info.center_y;
            info.vehicles_crossing++;
            //assign a crossing slot to help spread vehicles visually
            if (intersection == "F10") {
                g_vehicles[key].crossing_slot = g_f10_crossing_slot++;
            } else {
                g_vehicles[key].crossing_slot = g_f11_crossing_slot++;
            }
            
            //Decrement queue for this entry direction
            std::string queue_key = get_queue_key(intersection, g_vehicles[key].entry);
            if (g_queue_positions[queue_key] > 0) g_queue_positions[queue_key]--;
            
            //Move other waiting vehicles forward
            for (auto& [vid, veh] : g_vehicles) {
                if (veh.intersection == intersection && veh.entry == g_vehicles[key].entry && 
                    veh.state == VehicleState::ARRIVING && veh.queue_slot > 0) {
                    veh.queue_slot--;
                }
            }
        }
        return;
    }
    
    //Parse EXITING intersection
    std::regex exiting_regex(R"(Vehicle (\d+).*EXITING intersection (F\d+) towards (\w+))");
    if (std::regex_search(line, match, exiting_regex)) {
        std::string intersection = match[2].str();
        std::string key = intersection + "_" + match[1].str();
        if (g_vehicles.count(key)) {
            g_vehicles[key].state = VehicleState::EXITING;
            g_vehicles[key].destination = match[3].str();
            IntersectionInfo& info = (intersection == "F10") ? g_f10_info : g_f11_info;
            
            //Snap to center, then target exit edge
            g_vehicles[key].x = info.center_x;
            g_vehicles[key].y = info.center_y;
            
            auto exit_edge = get_exit_edge(info, match[3].str());
            g_vehicles[key].target_x = exit_edge.x;
            g_vehicles[key].target_y = exit_edge.y;
            if (info.vehicles_crossing > 0) info.vehicles_crossing--;
        }
        return;
    }
    
    //Parse PARKING - only at F10
    std::regex parking_regex(R"(Vehicle (\d+): PARKING)");
    if (std::regex_search(line, match, parking_regex)) {
        std::string key = "F10_" + match[1].str();
        if (g_vehicles.count(key)) {
            g_vehicles[key].state = VehicleState::PARKING;
            //Move to parking area
            g_vehicles[key].target_x = g_f10_info.center_x - 150.0f;
            g_vehicles[key].target_y = g_f10_info.center_y + 100.0f + (g_parking.parked_count % 5) * 25.0f;
        }
        return;
    }
    
    //Parse parking stats
    std::regex parked_regex(R"(Total parked: (\d+)/(\d+))");
    if (std::regex_search(line, match, parked_regex)) {
        g_parking.parked_count = std::stoi(match[1].str());
        g_parking.total_spots = std::stoi(match[2].str());
        return;
    }
    
    //Parse entering parking waiting queue
    std::regex entering_queue_regex(R"(Vehicle (\d+): Entering parking waiting queue)");
    if (std::regex_search(line, match, entering_queue_regex)) {
        g_parking.waiting_count++;
        if (g_parking.waiting_count > g_parking.queue_capacity) {
            g_parking.waiting_count = g_parking.queue_capacity;
        }
        return;
    }
    
    //Parse waiting for parking spot (vehicle is in queue, waiting for spot)
    std::regex waiting_parking_regex(R"(Vehicle (\d+): Waiting for parking spot)");
    if (std::regex_search(line, match, waiting_parking_regex)) {
        //Already in queue, just update state for the vehicle if needed
        return;
    }
    
    //Parse acquired parking spot - vehicle got a spot, leaves queue
    std::regex acquired_parking_regex(R"(Vehicle (\d+): ACQUIRED parking spot)");
    if (std::regex_search(line, match, acquired_parking_regex)) {
        //When vehicle acquires parking, it releases its queue slot
        //The waiting_count is decremented and parked_count is updated via the parked_regex
        if (g_parking.waiting_count > 0) g_parking.waiting_count--;
        return;
    }
    
    //Parse parking unavailable/rerouting (queue full)
    std::regex parking_unavail_regex(R"(Vehicle (\d+): (Parking unavailable|Waiting queue full|No parking))");
    if (std::regex_search(line, match, parking_unavail_regex)) {
        //Vehicle couldn't park, will exit system - no state change needed
        return;
    }
    
    //Parse transit - vehicle moves from one intersection to another via IPC
    //Example: "Vehicle 5 sent to F11 via pipe" or "Vehicle 5 transited from F10 to F11"
    std::regex transit_regex(R"(Vehicle (\d+) (transited|sent).*from (F\d+) to (F\d+)|Vehicle (\d+) sent to (F\d+) via pipe from (F\d+))");
    if (std::regex_search(line, match, transit_regex)) {
        int id = 0;
        std::string from_int, to_int;
        
        if (match[1].matched) {
            id = std::stoi(match[1].str());
            from_int = match[3].str();
            to_int = match[4].str();
        } else {
            id = std::stoi(match[5].str());
            to_int = match[6].str();
            from_int = match[7].str();
        }
        
        std::string old_key = from_int + "_" + std::to_string(id);
        
    //Mark vehicle as transiting and animate along road
        if (g_vehicles.count(old_key)) {
            g_vehicles[old_key].state = VehicleState::TRANSITING;
            //Target: other intersection edge
            IntersectionInfo& target_info = (to_int == "F10") ? g_f10_info : g_f11_info;
            auto edge = get_intersection_edge(target_info, (to_int == "F10") ? "East" : "West");
            g_vehicles[old_key].target_x = edge.x;
            g_vehicles[old_key].target_y = edge.y;
            g_stats.transit_count++;
            g_stats.ipc_messages++;
        }
        return;
    }
    
    //Also catch "received via pipe" at destination
    std::regex received_pipe_regex(R"(Vehicle (\d+) received via pipe at (F\d+))");
    if (std::regex_search(line, match, received_pipe_regex)) {
        g_stats.ipc_messages++;
        return;
    }
    
    //Parse exit from system - need to find which intersection
    std::regex exit_regex(R"(Vehicle (\d+) exited the system from (F\d+))");
    if (std::regex_search(line, match, exit_regex)) {
        std::string key = match[2].str() + "_" + match[1].str();
        g_vehicles.erase(key);
        return;
    }
    
    //Fallback exit pattern (no intersection specified)
    std::regex exit_simple_regex(R"(Vehicle (\d+) exited the system$)");
    if (std::regex_search(line, match, exit_simple_regex)) {
        std::string id_str = match[1].str();
    //Try both intersections
        g_vehicles.erase("F10_" + id_str);
        g_vehicles.erase("F11_" + id_str);
        return;
    }
    
    //Parse released parking spot
    std::regex released_parking_regex(R"(Vehicle (\d+): RELEASED parking spot)");
    if (std::regex_search(line, match, released_parking_regex)) {
        //parked_count is updated via parked_regex - this is informational
        return;
    }
    
    //Parse left parking - only at F10
    std::regex left_parking_regex(R"(Vehicle (\d+): Left parking)");
    if (std::regex_search(line, match, left_parking_regex)) {
        std::string key = "F10_" + match[1].str();
        g_vehicles.erase(key);
        return;
    }
    
    //Parse emergency mode
    if (line.find("EMERGENCY MODE ACTIVATED") != std::string::npos) {
        if (line.find("F10") != std::string::npos) g_f10_info.emergency_mode = true;
        if (line.find("F11") != std::string::npos) g_f11_info.emergency_mode = true;
        return;
    }
    if (line.find("EMERGENCY MODE DEACTIVATED") != std::string::npos) {
        if (line.find("F10") != std::string::npos) g_f10_info.emergency_mode = false;
        if (line.find("F11") != std::string::npos) g_f11_info.emergency_mode = false;
        return;
    }
}

// ============================================================================
//Drawing Functions
// ============================================================================
void draw_road(sf::RenderWindow& window, float x1, float y1, float x2, float y2, bool horizontal) {
    sf::RectangleShape road;
    if (horizontal) {
        road.setSize(sf::Vector2f(std::abs(x2 - x1), ROAD_WIDTH));
        road.setPosition(std::min(x1, x2), y1 - ROAD_WIDTH / 2);
    } else {
        road.setSize(sf::Vector2f(ROAD_WIDTH, std::abs(y2 - y1)));
        road.setPosition(x1 - ROAD_WIDTH / 2, std::min(y1, y2));
    }
    road.setFillColor(ROAD_COLOR);
    window.draw(road);
    
    //Center line
    sf::RectangleShape line;
    if (horizontal) {
        line.setSize(sf::Vector2f(std::abs(x2 - x1), 2));
        line.setPosition(std::min(x1, x2), y1 - 1);
    } else {
        line.setSize(sf::Vector2f(2, std::abs(y2 - y1)));
        line.setPosition(x1 - 1, std::min(y1, y2));
    }
    line.setFillColor(LINE_COLOR);
    window.draw(line);
}

void draw_intersection(sf::RenderWindow& window, const IntersectionInfo& info, const std::string& name, sf::Font& font) {
    //Intersection square
    sf::RectangleShape intersection;
    intersection.setSize(sf::Vector2f(INTERSECTION_SIZE, INTERSECTION_SIZE));
    intersection.setPosition(info.center_x - INTERSECTION_SIZE/2, info.center_y - INTERSECTION_SIZE/2);
    intersection.setFillColor(ROAD_COLOR);
    window.draw(intersection);
    
    //Emergency mode overlay with flashing effect
    if (info.emergency_mode) {
        sf::RectangleShape emergency_overlay;
        emergency_overlay.setSize(sf::Vector2f(INTERSECTION_SIZE + 20, INTERSECTION_SIZE + 20));
        emergency_overlay.setPosition(info.center_x - INTERSECTION_SIZE/2 - 10, info.center_y - INTERSECTION_SIZE/2 - 10);
        emergency_overlay.setFillColor(EMERGENCY_FLASH);
        emergency_overlay.setOutlineColor(sf::Color::Red);
        emergency_overlay.setOutlineThickness(4);
        window.draw(emergency_overlay);
        
    //Emergency text
        sf::Text emerg_text;
        emerg_text.setFont(font);
        emerg_text.setString("EMERGENCY");
        emerg_text.setCharacterSize(10);
        emerg_text.setFillColor(sf::Color::Red);
        emerg_text.setStyle(sf::Text::Bold);
        emerg_text.setPosition(info.center_x - 30, info.center_y - INTERSECTION_SIZE/2 - 25);
        window.draw(emerg_text);
    }
    
    //Intersection label
    sf::Text label;
    label.setFont(font);
    label.setString(name);
    label.setCharacterSize(18);
    label.setFillColor(sf::Color::White);
    label.setStyle(sf::Text::Bold);
    label.setPosition(info.center_x - 14, info.center_y - 10);
    window.draw(label);
    
    //Crossing count
    sf::Text crossing_text;
    crossing_text.setFont(font);
    crossing_text.setString(std::to_string(info.vehicles_crossing) + " crossing");
    crossing_text.setCharacterSize(9);
    crossing_text.setFillColor(sf::Color::Cyan);
    crossing_text.setPosition(info.center_x - 25, info.center_y + 15);
    window.draw(crossing_text);
}

void draw_parking_lot(sf::RenderWindow& window, sf::Font& font, unsigned int window_width, unsigned int window_height) {
    //Position parking box at left-bottom corner. Use window_width to size the box
    float px = 10.0f;
    float py = (float)window_height - 200.0f;
    float pwidth = std::clamp((float)window_width / 6.0f, 140.0f, 260.0f);
    
    //Parking area background
    sf::RectangleShape parking_bg;
    parking_bg.setSize(sf::Vector2f(pwidth, 180));
    parking_bg.setPosition(px, py);
    parking_bg.setFillColor(PARKING_COLOR);
    parking_bg.setOutlineColor(sf::Color::White);
    parking_bg.setOutlineThickness(2);
    window.draw(parking_bg);
    
    //Title
    sf::Text title;
    title.setFont(font);
    title.setString("PARKING LOT");
    title.setCharacterSize(11);
    title.setFillColor(sf::Color::Yellow);
    title.setStyle(sf::Text::Bold);
    title.setPosition(px + 25, py + 5);
    window.draw(title);
    
    //Parking spots grid (10 spots - 2 rows of 5)
    sf::Text spots_label;
    spots_label.setFont(font);
    spots_label.setString("Spots (10):");
    spots_label.setCharacterSize(9);
    spots_label.setFillColor(sf::Color::White);
    spots_label.setPosition(px + 10, py + 22);
    window.draw(spots_label);
    
    for (int i = 0; i < 10; i++) {
        sf::RectangleShape spot;
        spot.setSize(sf::Vector2f(22, 22));
        float sx = px + 10 + (i % 5) * 24;
        float sy = py + 38 + (i / 5) * 28;
        spot.setPosition(sx, sy);
        spot.setFillColor(i < g_parking.parked_count ? sf::Color(0, 180, 0) : sf::Color(40, 40, 40));
        spot.setOutlineColor(sf::Color::White);
        spot.setOutlineThickness(1);
        window.draw(spot);
        
    //Spot number
        if (i < g_parking.parked_count) {
            sf::Text num;
            num.setFont(font);
            num.setString("P");
            num.setCharacterSize(10);
            num.setFillColor(sf::Color::White);
            num.setPosition(sx + 6, sy + 4);
            window.draw(num);
        }
    }
    
    //Parking stats
    sf::Text parked_text;
    parked_text.setFont(font);
    parked_text.setString("Parked: " + std::to_string(g_parking.parked_count) + "/" + std::to_string(g_parking.total_spots));
    parked_text.setCharacterSize(10);
    parked_text.setFillColor(g_parking.parked_count >= g_parking.total_spots ? sf::Color::Red : sf::Color::Green);
    parked_text.setPosition(px + 10, py + 100);
    window.draw(parked_text);
    
    //Waiting queue section
    sf::Text queue_label;
    queue_label.setFont(font);
    queue_label.setString("Wait Queue (" + std::to_string(g_parking.queue_capacity) + "):");
    queue_label.setCharacterSize(9);
    queue_label.setFillColor(sf::Color::White);
    queue_label.setPosition(px + 10, py + 118);
    window.draw(queue_label);
    
    //Queue slots visualization
    for (int i = 0; i < g_parking.queue_capacity; i++) {
        sf::RectangleShape qslot;
        qslot.setSize(sf::Vector2f(20, 20));
        qslot.setPosition(px + 10 + i * 24, py + 135);
        qslot.setFillColor(i < g_parking.waiting_count ? sf::Color(200, 150, 0) : sf::Color(40, 40, 40));
        qslot.setOutlineColor(sf::Color(150, 150, 150));
        qslot.setOutlineThickness(1);
        window.draw(qslot);
        
        if (i < g_parking.waiting_count) {
            sf::Text w;
            w.setFont(font);
            w.setString("W");
            w.setCharacterSize(9);
            w.setFillColor(sf::Color::White);
            w.setPosition(px + 15 + i * 24, py + 138);
            window.draw(w);
        }
    }
    
    //Queue count
    sf::Text queue_text;
    queue_text.setFont(font);
    queue_text.setString("Waiting: " + std::to_string(g_parking.waiting_count) + "/" + std::to_string(g_parking.queue_capacity));
    queue_text.setCharacterSize(10);
    queue_text.setFillColor(g_parking.waiting_count >= g_parking.queue_capacity ? sf::Color::Red : sf::Color::Yellow);
    queue_text.setPosition(px + 10, py + 160);
    window.draw(queue_text);
}

void draw_vehicle(sf::RenderWindow& window, const VehicleInfo& v, sf::Font& font) {
    //Try to draw vehicle icon from resources; fallback to circle if not available
    std::string key = v.type;
    std::string k2 = normalize_key(key);
    bool drew_sprite = false;
    if (g_vehicle_textures.count(k2)) {
        const sf::Texture &tex = g_vehicle_textures[k2];
        sf::Sprite sprite;
        sprite.setTexture(tex);
    //center origin
        sprite.setOrigin((float)tex.getSize().x / 2.0f, (float)tex.getSize().y / 2.0f);
    //scale to VEHICLE_SIZE
        float sx = VEHICLE_SIZE / (float)tex.getSize().x;
        float sy = VEHICLE_SIZE / (float)tex.getSize().y;
        sprite.setScale(sx, sy);
        sprite.setPosition(v.x, v.y);
        window.draw(sprite);
        drew_sprite = true;
    }

    if (!drew_sprite) {
    //Draw vehicle as colored circle fallback
        sf::CircleShape vehicle(VEHICLE_SIZE / 2);
        vehicle.setFillColor(get_vehicle_color(v.type));
        if (v.is_emergency) {
            vehicle.setOutlineColor(sf::Color::Yellow);
            vehicle.setOutlineThickness(4);
        } else if (v.type == "Bus") {
            vehicle.setOutlineColor(sf::Color::Cyan);
            vehicle.setOutlineThickness(3);
        } else {
            vehicle.setOutlineColor(sf::Color::White);
            vehicle.setOutlineThickness(1);
        }
        vehicle.setPosition(v.x - VEHICLE_SIZE/2, v.y - VEHICLE_SIZE/2);
        window.draw(vehicle);
    }

    //Vehicle label (ensure readable over icon)
    sf::Text label;
    label.setFont(font);
    label.setString(get_vehicle_char(v.type) + std::to_string(v.id % 100));
    label.setCharacterSize(12);
    label.setFillColor(sf::Color::White);
    label.setStyle(sf::Text::Bold);
    label.setOutlineColor(sf::Color::Black);
    label.setOutlineThickness(1);
    label.setPosition(v.x - 8, v.y - 6);
    window.draw(label);

    //State indicator dot (small, drawn near vehicle)
    sf::CircleShape indicator(4);
    indicator.setPosition(v.x + VEHICLE_SIZE/2 - 8, v.y - VEHICLE_SIZE/2 - 6);
    if (v.state == VehicleState::WAITING) {
        indicator.setFillColor(sf::Color::Red);
        window.draw(indicator);
    } else if (v.state == VehicleState::CROSSING) {
        indicator.setFillColor(sf::Color::Green);
        window.draw(indicator);
    } else if (v.state == VehicleState::PARKING) {
        indicator.setFillColor(sf::Color::Blue);
        window.draw(indicator);
    } else if (v.state == VehicleState::TRANSITING) {
        indicator.setFillColor(sf::Color::Magenta);
        window.draw(indicator);
    }
}

void draw_log_panel(sf::RenderWindow& window, sf::Font& font, unsigned int window_width, unsigned int window_height) {
    //Background panel
    sf::RectangleShape panel;
    panel.setSize(sf::Vector2f(window_width - 20, 180));
    panel.setPosition(10, window_height - 190);
    panel.setFillColor(sf::Color(20, 20, 20, 230));
    panel.setOutlineColor(sf::Color::White);
    panel.setOutlineThickness(1);
    window.draw(panel);
    
    //Title
    sf::Text title;
    title.setFont(font);
    title.setString("Event Log");
    title.setCharacterSize(12);
    title.setFillColor(sf::Color::Yellow);
    title.setPosition(20, window_height - 185);
    window.draw(title);
    
    //Log lines
    std::lock_guard<std::mutex> lock(g_state_mutex);
    float y_offset = window_height - 170;
    for (const auto& line : g_log_lines) {
        sf::Text log_text;
        log_text.setFont(font);
    //Truncate long lines
        std::string display_line = line.length() > 80 ? line.substr(0, 77) + "..." : line;
        log_text.setString(display_line);
        log_text.setCharacterSize(10);
        log_text.setFillColor(sf::Color::White);
        log_text.setPosition(20, y_offset);
        window.draw(log_text);
        y_offset += 11;
    }
}

void draw_legend(sf::RenderWindow& window, sf::Font& font) {
    float lx = 10, ly = 10;
    
    sf::RectangleShape bg;
    bg.setSize(sf::Vector2f(165, 230));
    bg.setPosition(lx, ly);
    bg.setFillColor(sf::Color(20, 20, 20, 220));
    bg.setOutlineColor(sf::Color::White);
    bg.setOutlineThickness(1);
    window.draw(bg);
    
    sf::Text title;
    title.setFont(font);
    title.setString("VEHICLE TYPES");
    title.setCharacterSize(11);
    title.setFillColor(sf::Color::Yellow);
    title.setStyle(sf::Text::Bold);
    title.setPosition(lx + 10, ly + 5);
    window.draw(title);
    
    //Show vehicle type icons (loaded from resources/) instead of colored dots
    std::vector<std::string> types = {"Ambulance", "Firetruck", "Bus", "Car", "Bike", "Tractor"};
    float offset = ly + 25;
    const float icon_size = 18.0f;
    for (const auto& type : types) {
        std::string key = normalize_key(type);
        if (g_vehicle_textures.count(key)) {
            const sf::Texture &tex = g_vehicle_textures[key];
            sf::Sprite sprite;
            sprite.setTexture(tex);
            sprite.setOrigin((float)tex.getSize().x/2.0f, (float)tex.getSize().y/2.0f);
            float sx = icon_size / (float)tex.getSize().x;
            float sy = icon_size / (float)tex.getSize().y;
            sprite.setScale(sx, sy);
            sprite.setPosition(lx + 18, offset + 6);
            window.draw(sprite);
        } else {
            //fallback: colored dot
            sf::CircleShape dot(6);
            dot.setFillColor(get_vehicle_color(type));
            dot.setPosition(lx + 12, offset + 2);
            window.draw(dot);
        }

        sf::Text text;
        text.setFont(font);
        text.setString(type);
        text.setCharacterSize(10);
        text.setFillColor(sf::Color::White);
        text.setPosition(lx + 36, offset);
        window.draw(text);

        offset += 22;
    }
    
    //Priority explanation
    offset += 8;
    sf::Text priority_title;
    priority_title.setFont(font);
    priority_title.setString("PRIORITY LEVELS:");
    priority_title.setCharacterSize(9);
    priority_title.setFillColor(sf::Color::Cyan);
    priority_title.setPosition(lx + 10, offset);
    window.draw(priority_title);
    offset += 14;
    
    std::vector<std::string> priorities = {
        "[P1] Emergency - Preempts all",
        "[P2] Bus - Higher priority",
        "[P3] Normal - Standard"
    };
    for (const auto& p : priorities) {
        sf::Text pt;
        pt.setFont(font);
        pt.setString(p);
        pt.setCharacterSize(8);
        pt.setFillColor(sf::Color(200, 200, 200));
        pt.setPosition(lx + 15, offset);
        window.draw(pt);
        offset += 12;
    }
    
    //State indicators were moved to bottom-right panel per UI update
}

void draw_status_panel(sf::RenderWindow& window, sf::Font& font, unsigned int window_width) {
    float sx = window_width - 200, sy = 10;
    
    sf::RectangleShape bg;
    bg.setSize(sf::Vector2f(190, 200));
    bg.setPosition(sx, sy);
    bg.setFillColor(sf::Color(20, 20, 20, 220));
    bg.setOutlineColor(sf::Color::White);
    bg.setOutlineThickness(1);
    window.draw(bg);
    
    sf::Text title;
    title.setFont(font);
    title.setString("SIMULATION STATUS");
    title.setCharacterSize(11);
    title.setFillColor(sf::Color::Yellow);
    title.setStyle(sf::Text::Bold);
    title.setPosition(sx + 10, sy + 5);
    window.draw(title);
    
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    //Count vehicles by type and state
    int emergency_count = 0, transit_count = 0;
    int f10_vehicles = 0, f11_vehicles = 0;
    for (const auto& pair : g_vehicles) {
        const VehicleInfo& v = pair.second;
        if (v.is_emergency) emergency_count++;
        if (v.state == VehicleState::TRANSITING) transit_count++;
        if (v.intersection == "F10") f10_vehicles++;
        else if (v.intersection == "F11") f11_vehicles++;
    }
    
    float offset = sy + 25;
    
    //Active vehicles section
    sf::Text vehicles_label;
    vehicles_label.setFont(font);
    vehicles_label.setString("Active Vehicles: " + std::to_string(g_vehicles.size()));
    vehicles_label.setCharacterSize(10);
    vehicles_label.setFillColor(sf::Color::Green);
    vehicles_label.setPosition(sx + 10, offset);
    window.draw(vehicles_label);
    offset += 14;
    
    //Per-intersection counts
    sf::Text f10_label;
    f10_label.setFont(font);
    f10_label.setString("  F10: " + std::to_string(f10_vehicles) + "  F11: " + std::to_string(f11_vehicles));
    f10_label.setCharacterSize(9);
    f10_label.setFillColor(sf::Color(180, 180, 180));
    f10_label.setPosition(sx + 10, offset);
    window.draw(f10_label);
    offset += 16;
    
    //Emergency vehicles
    sf::Text emerg_label;
    emerg_label.setFont(font);
    emerg_label.setString("Emergency Vehicles: " + std::to_string(emergency_count));
    emerg_label.setCharacterSize(10);
    emerg_label.setFillColor(emergency_count > 0 ? sf::Color::Red : sf::Color::White);
    emerg_label.setPosition(sx + 10, offset);
    window.draw(emerg_label);
    offset += 16;
    
    //Transit (F10<->F11 IPC)
    sf::Text transit_label;
    transit_label.setFont(font);
    transit_label.setString("Transit (IPC): " + std::to_string(transit_count));
    transit_label.setCharacterSize(10);
    transit_label.setFillColor(sf::Color::Magenta);
    transit_label.setPosition(sx + 10, offset);
    window.draw(transit_label);
    offset += 18;
    
    //Intersection status section
    sf::Text inter_title;
    inter_title.setFont(font);
    inter_title.setString("INTERSECTION STATUS:");
    inter_title.setCharacterSize(9);
    inter_title.setFillColor(sf::Color::Cyan);
    inter_title.setPosition(sx + 10, offset);
    window.draw(inter_title);
    offset += 14;
    
    //F10 status
    sf::Text f10_status;
    f10_status.setFont(font);
    f10_status.setString("F10: " + std::string(g_f10_info.emergency_mode ? "EMERGENCY" : "Normal") + 
                         " (" + std::to_string(g_f10_info.vehicles_crossing) + " crossing)");
    f10_status.setCharacterSize(9);
    f10_status.setFillColor(g_f10_info.emergency_mode ? sf::Color::Red : sf::Color::Green);
    f10_status.setPosition(sx + 15, offset);
    window.draw(f10_status);
    offset += 13;
    
    //F11 status
    sf::Text f11_status;
    f11_status.setFont(font);
    f11_status.setString("F11: " + std::string(g_f11_info.emergency_mode ? "EMERGENCY" : "Normal") +
                         " (" + std::to_string(g_f11_info.vehicles_crossing) + " crossing)");
    f11_status.setCharacterSize(9);
    f11_status.setFillColor(g_f11_info.emergency_mode ? sf::Color::Red : sf::Color::Green);
    f11_status.setPosition(sx + 15, offset);
    window.draw(f11_status);
    offset += 18;
    
    //Parking status
    sf::Text parking_title;
    parking_title.setFont(font);
    parking_title.setString("PARKING LOT:");
    parking_title.setCharacterSize(9);
    parking_title.setFillColor(sf::Color::Cyan);
    parking_title.setPosition(sx + 10, offset);
    window.draw(parking_title);
    offset += 14;
    
    sf::Text parking_status;
    parking_status.setFont(font);
    parking_status.setString("Parked: " + std::to_string(g_parking.parked_count) + "/" + 
                             std::to_string(g_parking.total_spots) + 
                             "  Queue: " + std::to_string(g_parking.waiting_count) + "/" +
                             std::to_string(g_parking.queue_capacity));
    parking_status.setCharacterSize(9);
    parking_status.setFillColor(sf::Color::White);
    parking_status.setPosition(sx + 15, offset);
    window.draw(parking_status);
}

//Draw state indicators in bottom-right corner (moved from legend)
void draw_state_indicators_bottom_right(sf::RenderWindow& window, sf::Font& font, unsigned int window_width, unsigned int window_height) {
    float bw = 200, bh = 120;
    float sx = window_width - bw - 10;
    float sy = window_height - bh - 10;

    sf::RectangleShape bg;
    bg.setSize(sf::Vector2f(bw, bh));
    bg.setPosition(sx, sy);
    bg.setFillColor(sf::Color(20, 20, 20, 220));
    bg.setOutlineColor(sf::Color::White);
    bg.setOutlineThickness(1);
    window.draw(bg);

    sf::Text title;
    title.setFont(font);
    title.setString("STATE INDICATORS");
    title.setCharacterSize(11);
    title.setFillColor(sf::Color::Cyan);
    title.setStyle(sf::Text::Bold);
    title.setPosition(sx + 10, sy + 6);
    window.draw(title);

    float offset = sy + 28;
    std::vector<std::pair<std::string, sf::Color>> states = {
        {"Waiting", sf::Color::Red},
        {"Crossing", sf::Color::Green},
        {"Parking", sf::Color::Blue},
        {"Transit", sf::Color::Magenta}
    };

    for (const auto &s : states) {
        sf::CircleShape dot(6);
        dot.setFillColor(s.second);
        dot.setPosition(sx + 12, offset);
        window.draw(dot);

        sf::Text text;
        text.setFont(font);
        text.setString(s.first);
        text.setCharacterSize(12);
        text.setFillColor(sf::Color::White);
        text.setPosition(sx + 30, offset - 2);
        window.draw(text);

        offset += 22;
    }
}

// ============================================================================
// Animation Update
// ============================================================================
void update_vehicle_positions(float dt) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    const float SPEED = 140.0f; //pixels per second (increased for more visible motion)
    
    std::vector<std::string> to_remove;
    
    for (auto& [id, v] : g_vehicles) {
    //Update target for waiting vehicles based on their queue slot
        if (v.state == VehicleState::ARRIVING) {
            IntersectionInfo& info = (v.intersection == "F10") ? g_f10_info : g_f11_info;
            if (v.queue_slot == 0) {
                //Front of queue - target intersection edge
                auto edge = get_intersection_edge(info, v.entry);
                v.target_x = edge.x;
                v.target_y = edge.y;
            } else {
                //In queue - target queue position
                auto qpos = get_entry_position(info, v.entry, v.queue_slot - 1);
                v.target_x = qpos.x;
                v.target_y = qpos.y;
            }
        }
        
    //Move toward target
        float dx = v.target_x - v.x;
        float dy = v.target_y - v.y;
        float dist = std::sqrt(dx*dx + dy*dy);
        
        if (dist > 0.5f) {
            float move = SPEED * dt;
            if (move > dist) move = dist;
            v.x += (dx / dist) * move;
            v.y += (dy / dist) * move;
        }
        
    //Mark exiting vehicles for removal when they reach exit
        if (v.state == VehicleState::EXITING && dist < 5.0f) {
            //Continue to final exit position
            IntersectionInfo& info = (v.intersection == "F10") ? g_f10_info : g_f11_info;
            auto final_pos = get_exit_position(info, v.destination);
            float final_dist = std::sqrt(std::pow(final_pos.x - v.x, 2) + std::pow(final_pos.y - v.y, 2));
            
            if (final_dist > 5.0f) {
                v.target_x = final_pos.x;
                v.target_y = final_pos.y;
            }
        }
    }
}

// ============================================================================
// Main Visualization Loop
// ============================================================================
void run_visualization_loop(VisualizationConfig config) {
    printf("Initializing SFML window (%dx%d)...\n", config.window_width, config.window_height);
    fflush(stdout);
    
    sf::RenderWindow window(sf::VideoMode(config.window_width, config.window_height), 
                            "Traffic Simulation - SFML Visualization");
    
    if (!window.isOpen()) {
        printf("ERROR: Failed to create SFML window. Check your display settings.\n");
        printf("For WSL, ensure DISPLAY is set: export DISPLAY=:0\n");
        return;
    }
    
    printf("Window created successfully.\n");
    fflush(stdout);
    
    window.setFramerateLimit(config.target_fps);
    
    //Load font
    sf::Font font;
    //Try common font paths
    bool font_loaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf") ||
                       font.loadFromFile("/usr/share/fonts/TTF/DejaVuSans.ttf") ||
                       font.loadFromFile("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf") ||
                       font.loadFromFile("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf") ||
                       font.loadFromFile("/usr/share/fonts/truetype/freefont/FreeSans.ttf") ||
                       font.loadFromFile("C:\\Windows\\Fonts\\arial.ttf");
    
    if (!font_loaded) {
        printf("WARNING: Could not load any font. Text will not display.\n");
        printf("Install fonts with: sudo apt install fonts-dejavu-core\n");
        fflush(stdout);
    } else {
        printf("Font loaded successfully.\n");
        fflush(stdout);
    }

    //Preload vehicle icons from resources directories
    preload_vehicle_textures();

    //Background music (kept in scope for life of visualization)
    static sf::Music bg_music;
    bool bg_music_loaded = false;
    std::vector<std::string> music_candidates = {
        std::string("resources/traffic-in-city-309236.mp3"),
        std::string("resources/audio/traffic-in-city-309236.mp3"),
        std::string("assets/traffic-in-city-309236.mp3"),
        std::string("traffic-in-city-309236.mp3")
    };
    for (const auto &mfile : music_candidates) {
        if (bg_music.openFromFile(mfile)) {
            bg_music.setLoop(true);
            bg_music.setVolume(36.0f);
            bg_music.play();
            printf("[VIS] Background music loaded and playing: %s\n", mfile.c_str());
            fflush(stdout);
            bg_music_loaded = true;
            break;
        }
    }
    if (!bg_music_loaded) {
        printf("[VIS] Background music file not found in resources; look for traffic-in-city-309236.mp3\n");
        fflush(stdout);
    }
    
    //Log file monitoring
    std::ifstream log_file;
    std::streampos last_pos = 0;
    bool backlog_processed = false;
    sf::Clock log_poll_clock;
    sf::Clock frame_clock;
    
    printf("Entering main render loop. Waiting for simulation events...\n");
    fflush(stdout);
    
    int frame_count = 0;
    sf::Clock startup_clock;  //Ignore close events for first second (WSLg bug workaround)
    
    while (window.isOpen() && !g_shutdown_requested.load()) {
    //Handle events - MUST process all events each frame
        sf::Event event;
        while (window.pollEvent(event)) {
            //Ignore close events during first 1 second (WSLg sends spurious close events)
            bool can_close = startup_clock.getElapsedTime().asSeconds() > 1.0f;
            
            if (event.type == sf::Event::Closed && can_close) {
                printf("Window close event received.\n");
                g_user_exit.store(true);
                window.close();
            }
            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape && can_close) {
                printf("ESC key pressed.\n");
                g_user_exit.store(true);
                window.close();
            }
        }
        
    //Poll log file for updates - process lines gradually for animation
        if (log_poll_clock.getElapsedTime().asSeconds() >= config.log_poll_interval_sec) {
            log_poll_clock.restart();
            
            log_file.open(config.log_file);
            if (log_file.is_open()) {
                //On first successful open, locate the last simulation-start marker
                //and process the backlog from that point in one fast pass so all
                //ARRIVED/ENTERING/EXITING events for the current run are applied
                //immediately. Subsequent polls use a small-per-frame limit so
                //animation remains visible.
                if (!backlog_processed) {
                    std::string line;
                    std::streampos last_start_pos = 0;
                    //Walk file to find last marker position (position after the marker line)
                    while (std::getline(log_file, line)) {
                        if (line.find("=== Traffic Simulation Started ===") != std::string::npos) {
                            last_start_pos = log_file.tellg();
                        }
                    }

                    if (last_start_pos != 0) {
                        //Seek to just after the last start marker and parse everything
                        log_file.clear();
                        log_file.seekg(last_start_pos);
                    } else {
                        //No explicit marker found; start at beginning
                        log_file.clear();
                        log_file.seekg(0);
                    }

                    //Process the entire backlog in one pass for correct initial state
                    while (std::getline(log_file, line)) {
                        if (!line.empty()) parse_log_line(line);
                    }

                    //Nudge vehicles slightly so that vehicles parsed from backlog
                    //that already have target==position will visibly start moving.
                    {
                        std::lock_guard<std::mutex> state_lock(g_state_mutex);
                        for (auto &pair : g_vehicles) {
                            VehicleInfo &vv = pair.second;
                            float dx = vv.target_x - vv.x;
                            float dy = vv.target_y - vv.y;
                            float d = std::sqrt(dx*dx + dy*dy);
                            if (d < 2.0f) {
                                //Push the vehicle a few pixels away from its target so
                                //the next animation step moves it toward the target.
                                if (d > 0.0001f) {
                                    vv.x -= (dx / d) * 6.0f;
                                    vv.y -= (dy / d) * 6.0f;
                                } else {
                                    //If exactly overlapping, nudge in a default direction
                                    vv.x -= 6.0f;
                                    vv.y -= 2.0f;
                                }
                            }
                        }
                    }

                    last_pos = log_file.tellg();
                    backlog_processed = true;
                    log_file.close();
                } else {
                    //Regular incremental processing (limited lines per frame)
                    //Ensure last_pos is valid before seeking (tellg() can return -1 at EOF)
                    if (last_pos == std::streampos(-1)) last_pos = 0;
                    log_file.seekg(last_pos);
                    std::string line;
                    int lines_this_frame = 0;
                    const int MAX_LINES_PER_FRAME = 6;  //allow a few lines per frame for responsiveness

                    while (std::getline(log_file, line) && lines_this_frame < MAX_LINES_PER_FRAME) {
                        if (!line.empty()) {
                            parse_log_line(line);
                            lines_this_frame++;
                        }
                    }
                    last_pos = log_file.tellg();
                    log_file.close();
                }
            }
        }
        
    //Update animations
        float dt = frame_clock.restart().asSeconds();
        update_vehicle_positions(dt);
    //Use frame_count to avoid unused-variable warning; increment each frame
    frame_count++;
        
    //Clear and draw
        window.clear(GRASS_COLOR);
        
    //Draw roads connecting intersections
        draw_road(window, 0, g_f10_info.center_y, g_f10_info.center_x - INTERSECTION_SIZE/2, g_f10_info.center_y, true);
        draw_road(window, g_f10_info.center_x + INTERSECTION_SIZE/2, g_f10_info.center_y, 
                  g_f11_info.center_x - INTERSECTION_SIZE/2, g_f11_info.center_y, true);
        draw_road(window, g_f11_info.center_x + INTERSECTION_SIZE/2, g_f11_info.center_y, 
                  config.window_width, g_f11_info.center_y, true);
        
    //Vertical roads for F10
    draw_road(window, g_f10_info.center_x, 0, g_f10_info.center_x, g_f10_info.center_y - INTERSECTION_SIZE/2, false);
    draw_road(window, g_f10_info.center_x, g_f10_info.center_y + INTERSECTION_SIZE/2, 
          g_f10_info.center_x, config.window_height, false);
        
    //Vertical roads for F11
    draw_road(window, g_f11_info.center_x, 0, g_f11_info.center_x, g_f11_info.center_y - INTERSECTION_SIZE/2, false);
    draw_road(window, g_f11_info.center_x, g_f11_info.center_y + INTERSECTION_SIZE/2, 
          g_f11_info.center_x, config.window_height, false);
        
    //IPC Pipe indicator between F10 and F11
        {
            float ipc_y = g_f10_info.center_y - INTERSECTION_SIZE/2 - 40;
            float pipe_x1 = g_f10_info.center_x + INTERSECTION_SIZE/2 + 20;
            float pipe_x2 = g_f11_info.center_x - INTERSECTION_SIZE/2 - 20;
            
            //Pipe line
            sf::RectangleShape pipe_line;
            pipe_line.setSize(sf::Vector2f(pipe_x2 - pipe_x1, 4));
            pipe_line.setPosition(pipe_x1, ipc_y);
            pipe_line.setFillColor(sf::Color(150, 100, 200));
            window.draw(pipe_line);
            
            //Bidirectional arrows
            sf::Text arrow_label;
            arrow_label.setFont(font);
            arrow_label.setString("<-- IPC (Pipe) -->");
            arrow_label.setCharacterSize(10);
            arrow_label.setFillColor(sf::Color(200, 150, 255));
            arrow_label.setPosition((pipe_x1 + pipe_x2) / 2 - 55, ipc_y - 18);
            window.draw(arrow_label);
            
            //Endpoint indicators
            sf::CircleShape pipe_end1(6);
            pipe_end1.setFillColor(sf::Color(150, 100, 200));
            pipe_end1.setPosition(pipe_x1 - 6, ipc_y - 4);
            window.draw(pipe_end1);
            
            sf::CircleShape pipe_end2(6);
            pipe_end2.setFillColor(sf::Color(150, 100, 200));
            pipe_end2.setPosition(pipe_x2 - 6, ipc_y - 4);
            window.draw(pipe_end2);
        }
        
    //Draw intersections
        draw_intersection(window, g_f10_info, "F10", font);
        draw_intersection(window, g_f11_info, "F11", font);
        
    //Draw parking lot at left-bottom
    draw_parking_lot(window, font, config.window_width, config.window_height);
        
    //Draw vehicles
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            for (const auto& [id, v] : g_vehicles) {
                draw_vehicle(window, v, font);
            }
        }
        
    //Draw UI panels
    draw_legend(window, font);
    draw_status_panel(window, font, config.window_width);
    //Draw state indicators at bottom-right (moved from legend)
    draw_state_indicators_bottom_right(window, font, config.window_width, config.window_height);
        
    window.display();
    }
}

void request_visualization_shutdown() {
    g_shutdown_requested.store(true);
}

bool visualization_user_exit_requested() {
    return g_user_exit.load();
}
