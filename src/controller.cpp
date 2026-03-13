#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include "controller.hpp"
#include "vehicle.hpp"
#include "parking.hpp"
#include "utils.hpp"

//Phase 2: Initialize intersection state with quadrant mutexes
void init_intersection_state(IntersectionState* state) {
    //Initialize 4 quadrant mutexes
    for (int i = 0; i < 4; i++) {
        pthread_mutex_init(&state->quadrant_mutexes[i], NULL);
    }
    
    pthread_mutex_init(&state->emergency_lock, NULL);
    pthread_mutex_init(&state->count_lock, NULL);
    pthread_cond_init(&state->emergency_cond, NULL);  //Phase 5: Initialize condition variable
    pthread_cond_init(&state->vehicles_empty_cond, NULL);
    
    state->emergency_mode = false;
    state->emergency_vehicles_pending = 0;  //Phase 5: Initialize pending count
    state->vehicles_in_intersection = 0;
    state->active_vehicle_count = 0;
    
    log_event("Intersection state initialized with 4 quadrant mutexes (Phase 5: emergency preemption enabled)");
}

//Cleanup intersection state
void cleanup_intersection_state(IntersectionState* state) {
    for (int i = 0; i < 4; i++) {
        pthread_mutex_destroy(&state->quadrant_mutexes[i]);
    }
    pthread_mutex_destroy(&state->emergency_lock);
    pthread_mutex_destroy(&state->count_lock);
    pthread_cond_destroy(&state->emergency_cond);  //Phase 5: Destroy condition variable
    pthread_cond_destroy(&state->vehicles_empty_cond);
    
    log_event("Intersection state cleaned up");
}

void run_controller(IntersectionID id) {
    char log_msg[100];
    sprintf(log_msg, "Controller for Intersection %d started", id);
    log_event(log_msg);

    pthread_t generator_thread;
    IntersectionID arg = id;
    
    if (pthread_create(&generator_thread, NULL, vehicle_generator, &arg) != 0) {
        log_event("Failed to create vehicle generator thread");
        exit(1);
    }

    //Join generator (it runs forever, so this blocks)
    pthread_join(generator_thread, NULL);
}

//Phase 3: IPC listener thread
void* ipc_listener_routine(void* arg) {
    ControllerContext* ctx = (ControllerContext*)arg;
    
    char log_msg[256];
    sprintf(log_msg, "IPC Listener started for %s (reading from fd %d)", ctx->name, ctx->read_fd);
    log_event(log_msg);
    
    while (ctx->running) {
        IPCMessage msg;
        ssize_t bytes = read(ctx->read_fd, &msg, sizeof(IPCMessage));
        
        if (bytes == sizeof(IPCMessage)) {
            if (msg.type == TRANSIT) {
                sprintf(log_msg, "%s received TRANSIT message: Vehicle %d (%s) entering from %s", 
                        ctx->name, msg.vehicle_id, 
                        (msg.v_type == AMBULANCE ? "Ambulance" :
                         msg.v_type == FIRETRUCK ? "Firetruck" :
                         msg.v_type == BUS ? "Bus" :
                         msg.v_type == CAR ? "Car" :
                         msg.v_type == BIKE ? "Bike" : "Tractor"),
                        (msg.entry_point == NORTH ? "North" :
                         msg.entry_point == SOUTH ? "South" :
                         msg.entry_point == EAST ? "East" : "West"));
                log_event(log_msg);
                
                //Spawn new vehicle thread at this intersection
                VehicleArgs* vargs = new VehicleArgs();
                vargs->id = msg.vehicle_id;
                vargs->type = msg.v_type;
                vargs->entry = msg.entry_point;
                vargs->dest = msg.destination;
                vargs->intersection_id = ctx->id;
                vargs->arrival_time = time(NULL);
                vargs->priority = get_priority_for_type(vargs->type);
                vargs->controller_ctx = ctx;
                vargs->is_transit_vehicle = true;  //Phase 5: Mark as transit vehicle
                
                pthread_t thread;
                if (pthread_create(&thread, NULL, vehicle_routine, vargs) != 0) {
                    sprintf(log_msg, "%s failed to create vehicle thread for transit vehicle %d", 
                            ctx->name, msg.vehicle_id);
                    log_event(log_msg);
                    delete vargs;
                } else {
                    pthread_detach(thread);
                    sprintf(log_msg, "%s spawned transit vehicle %d", ctx->name, msg.vehicle_id);
                    log_event(log_msg);
                }
            }
            else if (msg.type == EMERGENCY_CLEAR) {
                sprintf(log_msg, "%s received EMERGENCY_CLEAR signal for Vehicle %d (%s)", 
                        ctx->name, msg.vehicle_id,
                        (msg.v_type == AMBULANCE ? "Ambulance" : "Firetruck"));
                log_event(log_msg);
                
                //Phase 5: Activate emergency mode to block normal vehicles
                pthread_mutex_lock(&ctx->state->emergency_lock);
                ctx->state->emergency_vehicles_pending++;
                ctx->state->emergency_mode = true;
                sprintf(log_msg, "%s EMERGENCY MODE ACTIVATED - Pending: %d, blocking normal traffic", 
                        ctx->name, ctx->state->emergency_vehicles_pending);
                log_event(log_msg);
                pthread_mutex_unlock(&ctx->state->emergency_lock);
            }
            else if (msg.type == EMERGENCY_PASSED) {
                sprintf(log_msg, "%s received EMERGENCY_PASSED signal for Vehicle %d", 
                        ctx->name, msg.vehicle_id);
                log_event(log_msg);
                
                //Phase 5: Decrement pending count and possibly deactivate emergency mode
                pthread_mutex_lock(&ctx->state->emergency_lock);
                ctx->state->emergency_vehicles_pending--;
                if (ctx->state->emergency_vehicles_pending <= 0) {
                    ctx->state->emergency_vehicles_pending = 0;
                    ctx->state->emergency_mode = false;
                    sprintf(log_msg, "%s EMERGENCY MODE DEACTIVATED - Normal traffic can resume", ctx->name);
                    log_event(log_msg);
                    //Wake up all waiting normal vehicles
                    pthread_cond_broadcast(&ctx->state->emergency_cond);
                }
                pthread_mutex_unlock(&ctx->state->emergency_lock);
            }
            else if (msg.type == SHUTDOWN) {
                sprintf(log_msg, "%s received SHUTDOWN signal", ctx->name);
                log_event(log_msg);
                ctx->running = false;
                
                //Clear emergency mode on shutdown to unblock any waiting vehicles
                pthread_mutex_lock(&ctx->state->emergency_lock);
                if (ctx->state->emergency_mode) {
                    ctx->state->emergency_mode = false;
                    ctx->state->emergency_vehicles_pending = 0;
                    sprintf(log_msg, "%s clearing emergency mode for shutdown", ctx->name);
                    log_event(log_msg);
                    pthread_cond_broadcast(&ctx->state->emergency_cond);
                }
                pthread_mutex_unlock(&ctx->state->emergency_lock);
                
                break;
            }
        } else if (bytes == 0) {
            //Pipe closed
            sprintf(log_msg, "%s IPC pipe closed", ctx->name);
            log_event(log_msg);
            break;
        } else if (bytes < 0) {
            //Read error - might be interrupted, continue if still running
            if (ctx->running) {
                usleep(10000);  //Sleep 10ms before retry
            } else {
                break;
            }
        }
    }
    
    sprintf(log_msg, "IPC Listener stopped for %s", ctx->name);
    log_event(log_msg);
    return NULL;
}

//Phase 3: Run controller with IPC support
void run_controller_with_ipc(ControllerContext* ctx) {
    char log_msg[256];
    sprintf(log_msg, "Controller %s started with IPC (write_fd=%d, read_fd=%d)", 
            ctx->name, ctx->write_fd, ctx->read_fd);
    log_event(log_msg);
    
    //Initialize intersection state
    IntersectionState state;
    init_intersection_state(&state);
    ctx->state = &state;
    ctx->running = true;
    
    //Phase 4: Initialize parking lot for F10 only
    ParkingLot parking;
    if (strcmp(ctx->name, "F10") == 0) {
        init_parking_lot(&parking, MAX_PARKING_SPOTS, MAX_WAITING_QUEUE);
        ctx->parking = &parking;
    } else {
        ctx->parking = NULL;
    }
    
    //Create IPC listener thread if read_fd is valid
    if (ctx->read_fd > 0) {
        if (pthread_create(&ctx->ipc_listener_thread, NULL, ipc_listener_routine, ctx) != 0) {
            log_event("Failed to create IPC listener thread");
            cleanup_intersection_state(&state);
            if (ctx->parking) cleanup_parking_lot(ctx->parking);
            exit(1);
        }
    }
    
    //Create vehicle generator thread
    pthread_t generator_thread;
    if (pthread_create(&generator_thread, NULL, vehicle_generator_with_context, ctx) != 0) {
        log_event("Failed to create vehicle generator thread");
        cleanup_intersection_state(&state);
        if (ctx->parking) cleanup_parking_lot(ctx->parking);
        exit(1);
    }
    
    //Wait for generator thread (runs until ctx->running becomes false)
    pthread_join(generator_thread, NULL);
    
    //Wait for IPC listener if it was created
    if (ctx->read_fd > 0) {
        pthread_join(ctx->ipc_listener_thread, NULL);
    }

    //Wait until all vehicles that entered the intersection have finished.
    //This prevents destroying quadrant mutexes while detached vehicle threads
    //may still be running and trying to lock/unlock them.
    pthread_mutex_lock(&state.count_lock);
    while (state.active_vehicle_count > 0) {
    //Wait on vehicles_empty_cond; it will be signaled when all
    //spawned vehicle threads have finished and active_vehicle_count
    //reaches zero.
        pthread_cond_wait(&state.vehicles_empty_cond, &state.count_lock);
    }
    pthread_mutex_unlock(&state.count_lock);
    
    //Cleanup
    cleanup_intersection_state(&state);
    if (ctx->parking) {
        cleanup_parking_lot(ctx->parking);
    }
    
    sprintf(log_msg, "Controller %s stopped", ctx->name);
    log_event(log_msg);
}
