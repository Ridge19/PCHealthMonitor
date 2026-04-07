CXX := g++
CXXFLAGS := -std=c++17 -pthread -Wall -Wextra -O2
TARGET := PCHealthMonitor

SRC := \
	PCHealthMonitor_Linux.cpp \
	PCHealthMonitorApp.cpp \
	TerminalUI.cpp \
	SystemMetrics.cpp

OBJ := $(SRC:.cpp=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
