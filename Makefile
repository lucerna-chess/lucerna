CXX ?= g++
EXE ?= lucerna
ARCH ?= native

SRC := $(wildcard src/*.cpp)
HDR := $(wildcard src/*.h)
EVALFILE ?= nets/default.nnue
HIDDEN ?= 512

CXXFLAGS := -std=c++20 -O3 -march=$(ARCH) -DNDEBUG -Wall -Wextra -DLUCERNA_HIDDEN=$(HIDDEN)
LDFLAGS := -static

# Embed the network into the binary when one exists.
ifneq ($(wildcard $(EVALFILE)),)
    CXXFLAGS += -DEVALFILE=\"$(EVALFILE)\"
    NETDEP := $(EVALFILE)
endif

ifeq ($(OS),Windows_NT)
    TARGET := $(EXE).exe
else
    TARGET := $(EXE)
endif

$(TARGET): $(SRC) $(HDR) $(NETDEP)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET)
