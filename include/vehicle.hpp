#ifndef VEHICLE_HPP
#define VEHICLE_HPP
#include "common.hpp"
//Phase 1
void* vehicle_routine(void* arg);
void* vehicle_generator(void* arg);

//Phase 2: Quadrant management (using C-style arrays instead of vectors)
int get_required_quadrants(Direction entry, Direction dest,int* quadrants);
void acquire_quadrants(pthread_mutex_t* mutexes,int* quads,int count,int vehicle_id);
void release_quadrants(pthread_mutex_t* mutexes,int* quads,int count,int vehicle_id);
int get_crossing_time(VehicleType type);

//Priority mapping helper
PriorityLevel get_priority_for_type(VehicleType type);

//Phase 3: Vehicle generator with context
void* vehicle_generator_with_context(void* arg);
#endif 
