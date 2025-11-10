CXX := x86_64-w64-mingw32-g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./imgui -I./imgui/backends -I./imgui/misc/cpp
LDFLAGS := -lopengl32 -lglu32 -lgdi32 -luser32 -limm32 -ldwmapi -lgdiplus
TARGET := build/opengl-demo.exe
IMGUI_DIR := imgui
IMGUI_SOURCES := \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMGUI_DIR)/misc/cpp/imgui_stdlib.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_win32.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_opengl2.cpp
SOURCES := src/main.cpp $(IMGUI_SOURCES)

all: $(TARGET)

$(TARGET): $(SOURCES) | build
	$(CXX) $(CXXFLAGS) -mwindows -static-libgcc -static-libstdc++ $(SOURCES) -o $@ $(LDFLAGS)

build:
	mkdir -p $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
