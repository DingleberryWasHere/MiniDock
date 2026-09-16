.DEFAULT_GOAL := all
CXX := g++
PKG_CONFIG ?= pkg-config
QT_MODULES := Qt6Widgets
PYTHON ?= python3
RCC ?= $(shell $(PKG_CONFIG) --variable=libexecdir Qt6Core)/rcc
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(QT_MODULES))
CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wpedantic -fPIC
LDLIBS += $(shell $(PKG_CONFIG) --libs $(QT_MODULES))
ifeq ($(OS),Windows_NT)
  TARGET := MiniDock.exe
  WINDRES ?= windres
  WINRES := minidock-v2.res.o
  LDFLAGS += -mwindows
  WINLIBS := -lshell32 -lole32 -luuid -luser32
  CPPFLAGS += -DNOMINMAX -DWIN32_LEAN_AND_MEAN
else
  TARGET := MiniDock
endif

.PHONY: all clean check-qt
all: $(TARGET)
check-qt:
	@$(PKG_CONFIG) --exists $(QT_MODULES) || (echo "Qt 6 development packages / pkg-config missing. See README.md."; exit 1)
$(TARGET): main.cpp launcher.cpp launcher.h settings_dialog.cpp settings_dialog.h dock_placement.h resources-v2.cpp $(WINRES) | check-qt
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) main.cpp launcher.cpp settings_dialog.cpp resources-v2.cpp $(WINRES) -o $@ $(LDFLAGS) $(LDLIBS) $(WINLIBS)
resources-v2.cpp: tools/strip_comments.py assets/minidock-v3.3.ico assets.qrc $(wildcard assets/*.png) assets/dusk-wallpaper.jpg assets/dock-shadow.png | check-qt
	$(RCC) assets.qrc -o $@
	$(PYTHON) tools/strip_comments.py $@
minidock-v2.res.o: windows-v2.rc assets/minidock-v3.3.ico
	$(WINDRES) -I. windows-v2.rc -o $@
clean:
	$(RM) MiniDock MiniDock.exe resources-v2.cpp minidock-v2.res.o
