#ifndef PARKING_HPP
#define PARKING_HPP

#include "common.hpp"

//Initialize parking lot with semaphores
void init_parking_lot(ParkingLot* parking,int spots,int queue_capacity);

//Cleanup parking lot resources
void cleanup_parking_lot(ParkingLot* parking);

//Check if vehicle type can park
bool can_vehicle_park(VehicleType type);

// Try to acquire parking (returns true if successful)
bool try_acquire_parking(ParkingLot* parking,int vehicle_id);

void release_parking(ParkingLot* parking,int vehicle_id);

void get_parking_stats(ParkingLot* parking,int* parked,int* waiting);
#endif