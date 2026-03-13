#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP
#include "common.hpp"

//Phase 1
void run_controller(IntersectionID id);

//Phase 3: Controller with IPC support
void run_controller_with_ipc(ControllerContext* ctx);

//IPC listener thread
void* ipc_listener_routine(void* arg);
void init_intersection_state(IntersectionState* state);

void cleanup_intersection_state(IntersectionState* state);
#endif
