CXX ?= g++
CXXFLAGS ?= -std=c++17 -pthread -Wall -Wextra -O2

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
RUN_CMD := .\
else
EXE_EXT :=
RUN_CMD := ./
endif

TARGET := PCHealthMonitor$(EXE_EXT)

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
	$(RUN_CMD)$(TARGET)

clean:
	rm -f $(OBJ) PCHealthMonitor PCHealthMonitor.exe