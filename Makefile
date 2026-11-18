# Cross-build of X3DAudio1_7.dll (32-bit Windows) with mingw-w64:  make
# Host check of every patch site against a real DP.exe:            make test DPEXE=/path/to/DP.exe
ifeq ($(origin CXX),default)
CXX := i686-w64-mingw32-g++
endif
HOSTCXX  ?= c++
OUT      := build
SRC      := src/main.cpp src/proxy.cpp src/log.cpp src/config.cpp src/pe.cpp src/patch.cpp src/rt.cpp \
            src/modules/qpc_time.cpp src/modules/zero_delta.cpp src/modules/saves.cpp src/modules/small_fixes.cpp src/modules/display.cpp src/modules/output_resolution.cpp src/modules/render_scale.cpp src/modules/world_distance.cpp src/modules/sony_layout.cpp src/d3d/trace.cpp src/modules/graphics_tweaks.cpp src/modules/gameplay.cpp src/modules/frame_pacer.cpp src/modules/camera_trace.cpp src/modules/input_auto.cpp src/modules/stick_profile.cpp src/modules/input_extras.cpp src/modules/crash_handler.cpp src/modules/world_extra.cpp src/modules/vehicle_triggers.cpp src/modules/vibration.cpp src/modules/xinput_input.cpp src/modules/dualsense.cpp src/modules/dualsense_out.cpp src/modules/adaptive_triggers.cpp src/modules/crash_guards.cpp src/modules/map_fix.cpp src/modules/freecam.cpp src/modules/ssao.cpp src/modules/blur_look.cpp src/modules/combat_strafe.cpp src/modules/world_alt3d.cpp src/modules/phys_freeze.cpp src/modules/cheats.cpp src/modules/launcher_install.cpp src/modules/cpu_tweaks.cpp src/modules/waypoint.cpp src/modules/texture_override.cpp src/ui/overlay.cpp src/ui/menu.cpp
SRC      += src/modules/text_overlay.cpp
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -fno-asynchronous-unwind-tables -fno-unwind-tables \
            -fno-stack-protector -fno-tree-loop-distribute-patterns -fno-ident
LDFLAGS  := -shared -nostdlib -nostartfiles -Wl,-e,_DllEntry@12 -Wl,--kill-at -Wl,--dynamicbase \
            -Wl,--nxcompat -s
LIBS     := -lkernel32 -luser32 -lgdi32 -lgcc

all: $(OUT)/X3DAudio1_7.dll $(OUT)/ufx_laa.exe $(OUT)/ufx_play.exe

src/launcher_stub.h: $(OUT)/ufx_play.exe
	( echo "// Generated from build/ufx_play.exe by the Makefile: the quick-start launcher the mod puts in place of DPLauncher.exe."; \
	  echo "static const unsigned char kLauncherStub[] = {"; xxd -i < $< ; echo "};" ) > $@

# Every header the sources include (src/ui, src/d3d, src/modules too) and the Makefile itself (a flag change rebuilds).
$(OUT)/X3DAudio1_7.dll: $(SRC) src/proxy.def src/launcher_stub.h $(wildcard src/*.h src/ui/*.h src/d3d/*.h src/modules/*.h) Makefile
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRC) src/proxy.def $(LIBS)

$(OUT)/ufx_laa.exe: tools/laa_patch.cpp Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Os -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-ident \
	    -nostdlib -nostartfiles -Wl,-e,_Start -Wl,--subsystem,windows -s -o $@ tools/laa_patch.cpp -lkernel32 -luser32 -lgcc

# No link timestamp: the same source gives the same bytes, so the stub embedded in the DLL matches a ufx_play.exe of the
# same release (the DLL also recognises other releases by their texts, launcher_install.cpp).
$(OUT)/ufx_play.exe: tools/ufx_play.cpp Makefile
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -Os -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-ident \
	    -nostdlib -nostartfiles -Wl,-e,_Begin -Wl,--subsystem,windows -Wl,--no-insert-timestamp -s -o $@ tools/ufx_play.cpp \
	    -lkernel32 -luser32 -lgcc

$(OUT)/verify_sites: tests/verify_sites.cpp src/sites.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Isrc -o $@ tests/verify_sites.cpp

$(OUT)/test_dsreport: tests/test_dsreport.cpp src/dsreport.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Isrc -o $@ tests/test_dsreport.cpp

$(OUT)/test_hash: tests/test_hash.cpp src/hash.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Isrc -o $@ tests/test_hash.cpp

$(OUT)/test_pacer: tests/test_pacer.cpp src/pacer.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Isrc -o $@ tests/test_pacer.cpp

# MES=/path/to/MES_ALL.MES also runs every record of a real file through the text encoder and back.
$(OUT)/test_mes: tests/test_mes.cpp src/mes.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Wall -Wextra -Isrc -o $@ tests/test_mes.cpp

test: $(OUT)/test_mes

$(OUT)/dump_stub: tests/dump_stub.cpp src/stubs.h src/sites.h
	@mkdir -p $(OUT)
	$(HOSTCXX) -std=c++17 -Isrc -o $@ tests/dump_stub.cpp

test: $(OUT)/verify_sites $(OUT)/dump_stub $(OUT)/test_pacer $(OUT)/test_hash $(OUT)/test_dsreport
	$(OUT)/test_pacer
	$(OUT)/test_hash
	$(OUT)/test_dsreport
	$(OUT)/test_mes docs/glyphmap.example.txt $(if $(MES),"$(MES)")
	$(OUT)/verify_sites "$(DPEXE)"
	$(OUT)/dump_stub $(OUT)/zero_delta_stub.bin $(OUT)/scale_thunk.bin $(OUT)/backbuffer_thunk.bin $(OUT)/hud_stub.bin $(OUT)/diff_store.bin $(OUT)/title_dialog.bin $(OUT)/aim_h.bin $(OUT)/aim_v.bin $(OUT)/stick_orig.bin $(OUT)/aim_shape.bin $(OUT)/vis_bridge.bin $(OUT)/trig1.bin $(OUT)/trig2.bin $(OUT)/trig3.bin $(OUT)/nav1.bin $(OUT)/nav2.bin $(OUT)/cambridge.bin $(OUT)/strafebridge.bin $(OUT)/entryhook.bin

clean:
	rm -rf $(OUT)

.PHONY: all test clean
