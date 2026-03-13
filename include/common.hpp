#ifndef COMMON_HPP
#define COMMON_HPP
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#//Constants
#define MAX_PARKING_SPOTS 10
#define MAX_WAITING_QUEUE 5 //Example bounded queue size
#define SIMULATION_TIME_SEC 30 //Default simulation time

#//Enums
typedef enum {
    AMBULANCE,
    FIRETRUCK,
    BUS,
    CAR,
    BIKE,
    TRACTOR
} VehicleType;

typedef enum {
    NORTH,
    SOUTH,
    EAST,
    WEST,
    PARKING  //Phase 4: Special destination for parking
} Direction;

typedef enum {
    INTERSECTION_F10,
    INTERSECTION_F11
} IntersectionID;

#//Vehicle Priority Levels (Phase 5 groundwork)
typedef enum {
    PRIORITY_EMERGENCY = 0, //Highest
    PRIORITY_HEAVY     = 1,
    PRIORITY_NORMAL    = 2  // Lowest
} PriorityLevel;

#//Phase 2: Quadrant System for Intersection
typedef enum {
    NW = 0,  //North-West
    NE = 1,  //North-East
    SW = 2,  //South-West
    SE = 3   //South-East
} Quadrant;

#//Phase 3: IPC Message Types
typedef enum {
    TRANSIT = 0,           //Vehicle transitioning between intersections
    EMERGENCY_CLEAR = 1,   //Emergency vehicle approaching
    ACK = 2,               //Acknowledgment
    SHUTDOWN = 3,          //Graceful shutdown signal
    EMERGENCY_PASSED = 4   //Phase 5: Emergency vehicle has passed
} MessageType;

#//Phase 2: Intersection State with Quadrant Mutexes
typedef struct {
    pthread_mutex_t quadrant_mutexes[4];
    volatile bool emergency_mode;           //Phase 5: Flag to block normal vehicles
    pthread_mutex_t emergency_lock;
    pthread_cond_t emergency_cond;          //Phase 5: Condition variable for waiting
    int emergency_vehicles_pending;         //Phase 5: Count of pending emergency vehicles
    int vehicles_in_intersection;
    pthread_cond_t vehicles_empty_cond;     //Signaled when active vehicle count reaches 0
    int active_vehicle_count;                //Number of vehicle threads currently alive for this intersection
    pthread_mutex_t count_lock;
} IntersectionState;

#//Phase 4: Parking Lot State
typedef struct {
    sem_t sem_parking_spots;    //Available parking spots (max 10)
    sem_t sem_waiting_queue;    //Bounded waiting queue slots
    int total_spots;            //Total capacity
    int queue_capacity;         //Max waiting queue size
    int parked_count;           //Current parked vehicles
    int waiting_count;          //Vehicles waiting for spot
    pthread_mutex_t stats_lock; //Protect counters
} ParkingLot;

#//Vehicle Structure
typedef struct {
    int id;
    VehicleType type;
    Direction entry;
    Direction dest;
    IntersectionID current_intersection;
    time_t arrival_time;
    PriorityLevel priority;
    //Add more fields as needed (speed, etc.)
} Vehicle;

#//Phase 3: IPC Message Structure
typedef struct {
    MessageType type;
    int vehicle_id;
    VehicleType v_type;
    Direction entry_point;  //For destination intersection
    Direction destination;
    time_t timestamp;
} IPCMessage;

#//Forward declaration for controller context
struct ControllerContext;

#//Vehicle Arguments for thread creation
typedef struct {
    int id;
    VehicleType type;
    Direction entry;
    Direction dest;
    IntersectionID intersection_id;
    time_t arrival_time;
    PriorityLevel priority;
    struct ControllerContext* controller_ctx;
    bool is_transit_vehicle;    //Phase 5: True if vehicle came from another intersection
} VehicleArgs;

#//Phase 3: Controller Context
typedef struct ControllerContext {
    char name[10];  //"F10" or "F11"
    int write_fd;   //Pipe to send messages to other intersection
    int read_fd;    //Pipe to receive messages from other intersection
    IntersectionState* state;
    ParkingLot* parking;        //Phase 4: Parking lot (only F10 has it)
    pthread_t ipc_listener_thread;
    IntersectionID id;
    volatile bool running;
    int max_vehicles; //<=0 means unlimited; generator stops at this count
} ControllerContext;
#endif 
