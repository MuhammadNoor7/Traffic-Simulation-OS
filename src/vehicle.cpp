#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include "vehicle.hpp"
#include "parking.hpp"
#include "utils.hpp"

const char* VehicleTypeNames[] = {
    "Ambulance", "Firetruck", "Bus", "Car", "Bike", "Tractor"
};

const char* DirectionNames[] = {
    "North", "South", "East", "West", "Parking"
};

//Phase 2: Determine which quadrants a vehicle needs based on entry and destination
//Returns number of quadrants needed, fills the quadrants array
int get_required_quadrants(Direction entry, Direction dest, int* quadrants) {
    int count = 0;
    
    //Phase 4: Parking destination - vehicle goes to parking area (uses specific quadrants based on entry)
    if (dest == PARKING) {
    //Parking area accessible from any entry, use 1-2 quadrants based on entry
        if (entry == NORTH) { quadrants[0] = NW; count = 1; }
        else if (entry == SOUTH) { quadrants[0] = SW; count = 1; }
        else if (entry == EAST) { quadrants[0] = NE; count = 1; }
        else if (entry == WEST) { quadrants[0] = NW; count = 1; }
        return count;
    }
    
    //Straight paths (2 quadrants)
    if (entry == NORTH && dest == SOUTH) { 
        quadrants[0] = NW; quadrants[1] = SW; count = 2;
    }
    else if (entry == SOUTH && dest == NORTH) { 
        quadrants[0] = NE; quadrants[1] = SE; count = 2;
    }
    else if (entry == EAST && dest == WEST) { 
        quadrants[0] = NE; quadrants[1] = NW; count = 2;
    }
    else if (entry == WEST && dest == EAST) { 
        quadrants[0] = SE; quadrants[1] = SW; count = 2;
    }
    
    //Right turns (1 quadrant)
    else if (entry == NORTH && dest == EAST) { 
        quadrants[0] = NE; count = 1;
    }
    else if (entry == EAST && dest == SOUTH) { 
        quadrants[0] = SE; count = 1;
    }
    else if (entry == SOUTH && dest == WEST) { 
        quadrants[0] = SW; count = 1;
    }
    else if (entry == WEST && dest == NORTH) { 
        quadrants[0] = NW; count = 1;
    }
    
    //Left turns (3 quadrants) - vehicle crosses through the intersection
    //Intersection layout:
    //        NORTH
    //          |
    //    NW    |    NE
    //  --------|--------
    //WEST      |      EAST
    //  --------|--------
    //    SW    |    SE
    //          |
    //        SOUTH
    //
    //For left turns, vehicle enters, crosses center, then exits on left side
    else if (entry == NORTH && dest == WEST) { 
        //Enter from North (NE side), cross to NW, then exit West
        quadrants[0] = NE; quadrants[1] = NW; quadrants[2] = SW; count = 3;
    }
    else if (entry == WEST && dest == SOUTH) { 
        //Enter from West (NW side), cross to SW, then exit South
        quadrants[0] = NW; quadrants[1] = SW; quadrants[2] = SE; count = 3;
    }
    else if (entry == SOUTH && dest == EAST) { 
        //Enter from South (SW side), cross to SE, then exit East
        quadrants[0] = SW; quadrants[1] = SE; quadrants[2] = NE; count = 3;
    }
    else if (entry == EAST && dest == NORTH) { 
        //Enter from East (SE side), cross to NE, then exit North
        quadrants[0] = SE; quadrants[1] = NE; quadrants[2] = NW; count = 3;
    }
    
    //Sort quadrants to ensure consistent lock ordering (bubble sort)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (quadrants[j] > quadrants[j + 1]) {
                int temp = quadrants[j];
                quadrants[j] = quadrants[j + 1];
                quadrants[j + 1] = temp;
            }
        }
    }
    
    return count;
}

//Phase 2: Acquire quadrants in order
void acquire_quadrants(pthread_mutex_t* mutexes, int* quads, int count, int vehicle_id) {
    for (int i = 0; i < count; i++) {
        pthread_mutex_lock(&mutexes[quads[i]]);
        char log_msg[256];
        sprintf(log_msg, "Vehicle %d acquired quadrant %d", vehicle_id, quads[i]);
        log_event(log_msg);
    }
}

//Phase 2: Release quadrants
void release_quadrants(pthread_mutex_t* mutexes, int* quads, int count, int vehicle_id) {
    for (int i = 0; i < count; i++) {
        pthread_mutex_unlock(&mutexes[quads[i]]);
        char log_msg[256];
        sprintf(log_msg, "Vehicle %d released quadrant %d", vehicle_id, quads[i]);
        log_event(log_msg);
    }
}

//Phase 2: Get crossing time based on vehicle type (in milliseconds)
int get_crossing_time(VehicleType type) {
    switch(type) {
        case AMBULANCE: return 1000;  // 1 second (fastest)
        case FIRETRUCK: return 1200;  // 1.2 seconds
        case CAR:       return 1500;  // 1.5 seconds
        case BUS:       return 2000;  // 2 seconds
        case BIKE:      return 1800;  // 1.8 seconds
        case TRACTOR:   return 3000;  // 3 seconds (slowest)
        default:        return 2000;
    }
}

//Map vehicle type to priority level
PriorityLevel get_priority_for_type(VehicleType type) {
    if (type == AMBULANCE || type == FIRETRUCK) return PRIORITY_EMERGENCY;
    if (type == BUS || type == TRACTOR) return PRIORITY_HEAVY;
    return PRIORITY_NORMAL; // CAR, BIKE
}

void* vehicle_routine(void* arg) {
    VehicleArgs* vargs = (VehicleArgs*)arg;
    char log_msg[256];

    sprintf(log_msg, "Vehicle %d (%s) Arrived at %s intersection %s", 
        vargs->id, VehicleTypeNames[vargs->type], 
        vargs->controller_ctx->name, DirectionNames[vargs->entry]);
    log_event(log_msg);

    //Count this thread as active for the controller so the controller can
    //wait for all spawned vehicle threads to finish before cleaning up.
    pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
    vargs->controller_ctx->state->active_vehicle_count++;
    pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);

    //Phase 5: Check if this is an emergency vehicle
    bool is_emergency = (vargs->type == AMBULANCE || vargs->type == FIRETRUCK);
    
    //Phase 5: Normal vehicles must wait if emergency mode is active
    //Added timeout to prevent permanent blocking if EMERGENCY_PASSED message is lost
    if (!is_emergency) {
        pthread_mutex_lock(&vargs->controller_ctx->state->emergency_lock);
        
        //Use timed wait with 10 second timeout to prevent permanent blocking
        struct timespec timeout;
        int wait_count = 0;
        const int MAX_EMERGENCY_WAIT_CYCLES = 20; //20 * 500ms = 10 seconds max wait
        
        while (vargs->controller_ctx->state->emergency_mode && wait_count < MAX_EMERGENCY_WAIT_CYCLES) {
            if (wait_count == 0) {
                sprintf(log_msg, "Vehicle %d (%s) WAITING - Emergency mode active at %s",
                        vargs->id, VehicleTypeNames[vargs->type], vargs->controller_ctx->name);
                log_event(log_msg);
            }
            
            //Use timed wait (500ms) to allow periodic checking
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += 500000000; //500ms
            if (timeout.tv_nsec >= 1000000000) {
                timeout.tv_sec += 1;
                timeout.tv_nsec -= 1000000000;
            }
            
            pthread_cond_timedwait(&vargs->controller_ctx->state->emergency_cond,
                                   &vargs->controller_ctx->state->emergency_lock,
                                   &timeout);
            wait_count++;
        }
        
        //If we timed out while still in emergency mode, force clear it
        if (vargs->controller_ctx->state->emergency_mode && wait_count >= MAX_EMERGENCY_WAIT_CYCLES) {
            sprintf(log_msg, "Vehicle %d: Emergency mode timeout at %s - force clearing stuck state",
                    vargs->id, vargs->controller_ctx->name);
            log_event(log_msg);
            
            //Reset emergency state to prevent permanent blockage
            vargs->controller_ctx->state->emergency_mode = false;
            vargs->controller_ctx->state->emergency_vehicles_pending = 0;
            pthread_cond_broadcast(&vargs->controller_ctx->state->emergency_cond);
            
            sprintf(log_msg, "%s EMERGENCY MODE FORCE CLEARED - traffic resuming",
                    vargs->controller_ctx->name);
            log_event(log_msg);
        }
        
        pthread_mutex_unlock(&vargs->controller_ctx->state->emergency_lock);
    } else {
        sprintf(log_msg, "Vehicle %d (%s) EMERGENCY VEHICLE - Priority passage at %s",
                vargs->id, VehicleTypeNames[vargs->type], vargs->controller_ctx->name);
        log_event(log_msg);
    }

    //Phase 4: Handle parking destination
    bool going_to_park = (vargs->dest == PARKING);
    bool parking_acquired = false;
    
    if (going_to_park) {
    //Try to acquire parking BEFORE entering intersection
        if (vargs->controller_ctx->parking != NULL) {
            parking_acquired = try_acquire_parking(vargs->controller_ctx->parking, vargs->id);
            
            if (!parking_acquired) {
                sprintf(log_msg, "Vehicle %d: Parking unavailable, exiting system", vargs->id);
                log_event(log_msg);
                //decrement active vehicle count before exiting
                pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
                vargs->controller_ctx->state->active_vehicle_count--;
                if (vargs->controller_ctx->state->active_vehicle_count <= 0) {
                    vargs->controller_ctx->state->active_vehicle_count = 0;
                    pthread_cond_signal(&vargs->controller_ctx->state->vehicles_empty_cond);
                }
                pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);
                delete vargs;
                return NULL;
            }
        } else {
            sprintf(log_msg, "Vehicle %d: No parking at %s, exiting", vargs->id, vargs->controller_ctx->name);
            log_event(log_msg);
            //decrement active vehicle count before exiting
            pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
            vargs->controller_ctx->state->active_vehicle_count--;
            if (vargs->controller_ctx->state->active_vehicle_count <= 0) {
                vargs->controller_ctx->state->active_vehicle_count = 0;
                pthread_cond_signal(&vargs->controller_ctx->state->vehicles_empty_cond);
            }
            pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);
            delete vargs;
            return NULL;
        }
    }

    //Phase 2: Get required quadrants
    int quadrants[4];  //Max 3 quadrants needed
    int quad_count = get_required_quadrants(vargs->entry, vargs->dest, quadrants);
    
    sprintf(log_msg, "Vehicle %d requesting %d quadrant(s) for %s->%s", 
            vargs->id, quad_count, DirectionNames[vargs->entry], DirectionNames[vargs->dest]);
    log_event(log_msg);

    //Phase 2: Acquire quadrants (safe crossing)
    acquire_quadrants(vargs->controller_ctx->state->quadrant_mutexes, quadrants, quad_count, vargs->id);
    
    sprintf(log_msg, "Vehicle %d (%s) ENTERING intersection %s", 
            vargs->id, VehicleTypeNames[vargs->type], vargs->controller_ctx->name);
    log_event(log_msg);

    // Mark that a vehicle is now inside the intersection
    pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
    vargs->controller_ctx->state->vehicles_in_intersection++;
    pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);

    // Simulate crossing time based on vehicle type
    int crossing_time = get_crossing_time(vargs->type);
    usleep(crossing_time * 1000);

    sprintf(log_msg, "Vehicle %d (%s) EXITING intersection %s towards %s", 
            vargs->id, VehicleTypeNames[vargs->type], 
            vargs->controller_ctx->name, DirectionNames[vargs->dest]);
    log_event(log_msg);

    //Phase 2: Release quadrants
    release_quadrants(vargs->controller_ctx->state->quadrant_mutexes, quadrants, quad_count, vargs->id);

    //Mark vehicle leaving intersection
    pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
    vargs->controller_ctx->state->vehicles_in_intersection--;
    if (vargs->controller_ctx->state->vehicles_in_intersection <= 0) {
        vargs->controller_ctx->state->vehicles_in_intersection = 0;
        // Signal any waiter that intersection is empty
        pthread_cond_signal(&vargs->controller_ctx->state->vehicles_empty_cond);
    }
    pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);

    //Phase 5: If this is an emergency transit vehicle, send EMERGENCY_PASSED to source intersection
    if (is_emergency && vargs->is_transit_vehicle && vargs->controller_ctx->write_fd > 0) {
        IPCMessage passed_msg;
        passed_msg.type = EMERGENCY_PASSED;
        passed_msg.vehicle_id = vargs->id;
        passed_msg.v_type = vargs->type;
        passed_msg.timestamp = time(NULL);
        
        ssize_t written = write(vargs->controller_ctx->write_fd, &passed_msg, sizeof(IPCMessage));
        if (written > 0) {
            sprintf(log_msg, "Vehicle %d (%s) sent EMERGENCY_PASSED to %s - clearing emergency mode", 
                    vargs->id, VehicleTypeNames[vargs->type],
                    strcmp(vargs->controller_ctx->name, "F10") == 0 ? "F11" : "F10");
            log_event(log_msg);
        }
    }

    //Phase 4: If parking, simulate parking duration then leave
    if (parking_acquired) {
        sprintf(log_msg, "Vehicle %d: PARKING (duration: 3-7 seconds)", vargs->id);
        log_event(log_msg);
        
    //Simulate parking duration (3-7 seconds)
        int park_duration = (rand() % 5 + 3) * 1000000; // 3-7 seconds in microseconds
        usleep(park_duration);
        
    //Leave parking
        release_parking(vargs->controller_ctx->parking, vargs->id);
        
        sprintf(log_msg, "Vehicle %d: Left parking, exiting system", vargs->id);
        log_event(log_msg);
        
    //decrement active vehicle count before exiting
        pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
        vargs->controller_ctx->state->active_vehicle_count--;
        if (vargs->controller_ctx->state->active_vehicle_count <= 0) {
            vargs->controller_ctx->state->active_vehicle_count = 0;
            pthread_cond_signal(&vargs->controller_ctx->state->vehicles_empty_cond);
        }
        pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);

        delete vargs;
        return NULL;
    }

    //Phase 3: Check if vehicle transits to other intersection
    if (vargs->controller_ctx->write_fd > 0) {  //IPC enabled
        bool should_transit = false;
        Direction new_entry = NORTH;
        
        if (strcmp(vargs->controller_ctx->name, "F10") == 0 && vargs->dest == EAST) {
            should_transit = true;
            new_entry = WEST;  // Enters F11 from west
        }
        else if (strcmp(vargs->controller_ctx->name, "F11") == 0 && vargs->dest == WEST) {
            should_transit = true;
            new_entry = EAST;  // Enters F10 from east
        }
        
        if (should_transit) {
            //Phase 5: Emergency vehicles send EMERGENCY_CLEAR signal BEFORE transit
            if (is_emergency) {
                IPCMessage clear_msg;
                clear_msg.type = EMERGENCY_CLEAR;
                clear_msg.vehicle_id = vargs->id;
                clear_msg.v_type = vargs->type;
                clear_msg.entry_point = new_entry;
                clear_msg.destination = vargs->dest;
                clear_msg.timestamp = time(NULL);
                
                ssize_t written = write(vargs->controller_ctx->write_fd, &clear_msg, sizeof(IPCMessage));
                if (written > 0) {
                    sprintf(log_msg, "Vehicle %d (%s) sent EMERGENCY_CLEAR to %s", 
                            vargs->id, VehicleTypeNames[vargs->type],
                            strcmp(vargs->controller_ctx->name, "F10") == 0 ? "F11" : "F10");
                    log_event(log_msg);
                }
            }
            
            //Send TRANSIT message
            IPCMessage msg;
            msg.type = TRANSIT;
            msg.vehicle_id = vargs->id;
            msg.v_type = vargs->type;
            msg.entry_point = new_entry;
            //Continue in same direction after transit
            msg.destination = vargs->dest;
            msg.timestamp = time(NULL);
            
            ssize_t written = write(vargs->controller_ctx->write_fd, &msg, sizeof(IPCMessage));
            if (written > 0) {
                sprintf(log_msg, "Vehicle %d transited from %s to %s", 
                        vargs->id, vargs->controller_ctx->name, 
                        strcmp(vargs->controller_ctx->name, "F10") == 0 ? "F11" : "F10");
                log_event(log_msg);
            } else {
                sprintf(log_msg, "Vehicle %d failed to send transit message", vargs->id);
                log_event(log_msg);
            }
        } else {
            //Phase 5: Emergency vehicle exiting system - send EMERGENCY_PASSED to clear mode
            //(This applies when emergency vehicle exits without transiting to another intersection)
            if (is_emergency) {
                //Clear emergency mode at current intersection
                pthread_mutex_lock(&vargs->controller_ctx->state->emergency_lock);
                //Note: Emergency mode for local intersection is handled by condition in main routine
                pthread_mutex_unlock(&vargs->controller_ctx->state->emergency_lock);
            }
            
            sprintf(log_msg, "Vehicle %d exited the system from %s", 
                    vargs->id, vargs->controller_ctx->name);
            log_event(log_msg);
        }
    } else {
        sprintf(log_msg, "Vehicle %d exited the system", vargs->id);
        log_event(log_msg);
    }

    //decrement active vehicle count for normal exit
    pthread_mutex_lock(&vargs->controller_ctx->state->count_lock);
    vargs->controller_ctx->state->active_vehicle_count--;
    (void)0; //no-op placeholder to avoid unused-value issues
    if (vargs->controller_ctx->state->active_vehicle_count <= 0) {
        vargs->controller_ctx->state->active_vehicle_count = 0;
        pthread_cond_signal(&vargs->controller_ctx->state->vehicles_empty_cond);
    }
    pthread_mutex_unlock(&vargs->controller_ctx->state->count_lock);

    delete vargs;
    return NULL;
}

void* vehicle_generator(void* arg) {
    IntersectionID* id = (IntersectionID*)arg;
    int vehicle_count = 0;
    
    char start_msg[100];
    sprintf(start_msg, "Vehicle Generator started for Intersection %d", *id);
    log_event(start_msg);

    while (1) {
        Vehicle* v = (Vehicle*)malloc(sizeof(Vehicle));
        v->id = ++vehicle_count;
        v->type = (VehicleType)(rand() % 6);
        v->entry = (Direction)(rand() % 4);
        do {
            v->dest = (Direction)(rand() % 4);
        } while (v->dest == v->entry); // Destination must be different from entry
        v->current_intersection = *id;

        pthread_t tid;
        if (pthread_create(&tid, NULL, vehicle_routine, v) != 0) {
            log_event("Failed to create vehicle thread");
            free(v);
        } else {
            pthread_detach(tid);
        }

        // Random spawn interval
        usleep((rand() % 1000 + 500) * 1000); // 0.5-1.5 seconds
    }
    return NULL;
}

//Phase 3: Vehicle generator with controller context
void* vehicle_generator_with_context(void* arg) {
    ControllerContext* ctx = (ControllerContext*)arg;
    int vehicle_count = 0;
    
    char start_msg[100];
    sprintf(start_msg, "Vehicle Generator started for Intersection %s", ctx->name);
    log_event(start_msg);

    while (ctx->running && (ctx->max_vehicles <= 0 || vehicle_count < ctx->max_vehicles)) {
        VehicleArgs* vargs = new VehicleArgs();
        vargs->id = ++vehicle_count;
        vargs->type = (VehicleType)(rand() % 6);
        vargs->entry = (Direction)(rand() % 4);
        vargs->arrival_time = time(NULL);
        vargs->priority = get_priority_for_type(vargs->type);
        
        // Phase 4: Decide if vehicle goes to parking (30% chance for parkable vehicles at F10)
        bool wants_parking = false;
        if (ctx->parking != NULL && can_vehicle_park(vargs->type) && (rand() % 100 < 30)) {
            wants_parking = true;
            vargs->dest = PARKING;
        } else {
            // Generate destination different from entry
            do {
                vargs->dest = (Direction)(rand() % 4);
            } while (vargs->dest == vargs->entry);
        }
        
        vargs->intersection_id = ctx->id;
        vargs->controller_ctx = ctx;
        vargs->is_transit_vehicle = false;  // Phase 5: Locally spawned, not a transit vehicle

        pthread_t tid;
        if (pthread_create(&tid, NULL, vehicle_routine, vargs) != 0) {
            log_event("Failed to create vehicle thread");
            delete vargs;
        } else {
            pthread_detach(tid);
            if (wants_parking) {
                sprintf(start_msg, "Vehicle %d requested parking at %s", vargs->id, ctx->name);
                log_event(start_msg);
            }
        }

        // Random spawn interval: 0.5-1.5 seconds
        usleep((rand() % 1000 + 500) * 1000);
    }
    // Stop controller after reaching max vehicles
    ctx->running = false;
    
    sprintf(start_msg, "Vehicle Generator stopped for Intersection %s", ctx->name);
    log_event(start_msg);
    return NULL;
}
