CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread -Iinclude
LDFLAGS_SIM = -pthread
LDFLAGS_VIS = -pthread -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# Source files for simulation (exclude visualizer_main.cpp and display.cpp)
SIM_SRCS = $(filter-out $(SRC_DIR)/visualizer_main.cpp $(SRC_DIR)/display.cpp, $(wildcard $(SRC_DIR)/*.cpp))
SIM_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SIM_SRCS))

# Source files for visualizer
VIS_SRCS = $(SRC_DIR)/visualizer_main.cpp $(SRC_DIR)/display.cpp
VIS_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(VIS_SRCS))

TARGET_SIM = $(BIN_DIR)/traffic_sim
TARGET_VIS = $(BIN_DIR)/traffic_visualizer

.PHONY: all clean directories sim vis

all: directories $(TARGET_SIM) $(TARGET_VIS)

sim: directories $(TARGET_SIM)

vis: directories $(TARGET_VIS)

$(TARGET_SIM): $(SIM_OBJS)
	$(CXX) $(SIM_OBJS) -o $@ $(LDFLAGS_SIM)

$(TARGET_VIS): $(VIS_OBJS)
	$(CXX) $(VIS_OBJS) -o $@ $(LDFLAGS_VIS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) logs

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
