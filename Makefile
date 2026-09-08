include common.mk

SHIJIMA_USE_QTMULTIMEDIA ?= 1

PREFIX ?= /usr/local

ifeq ($(PLATFORM),macOS)
PLATFORM_SYSTEM_SOURCES = src/system/HotkeyManager_mac.mm \
	src/system/SystemObserver_mac.mm
endif

ifeq ($(PLATFORM),Windows)
PLATFORM_SYSTEM_SOURCES = src/system/HotkeyManager_win.cc \
	src/system/SystemObserver_win.cc
TARGET_LDFLAGS += -lws2_32 -lole32 -lshell32 -luser32 -lkernel32 -lpsapi -lsqlite3
endif


ifeq ($(PLATFORM),Linux)
PLATFORM_SYSTEM_SOURCES = src/system/HotkeyManager_linux.cc \
	src/system/SystemObserver_linux.cc
endif

SOURCES = src/main.cc \
	src/core/SettingsDb.cc \
	src/core/Asset.cc \
	src/core/MascotData.cc \
	src/core/AssetLoader.cc \
	src/core/DefaultMascot.cc \
	src/core/SoundEffectManager.cc \
	src/core/cli.cc \
	src/pet/ShijimaManager.cc \
	src/pet/ShijimaWidget.cc \
	src/pet/PetMotionController.cc \
	src/pet/PetEventBus.cc \
	src/pet/PetMemory.cc \
	src/pet/BehaviorEngine.cc \
	src/pet/ReactionEngine.cc \
	src/pet/InitiativeTrigger.cc \
	src/pet/FileDisposalSequence.cc \
	src/pet/PetDiaryManager.cc \
	src/agent/AgentService.cc \
	src/agent/LongTermMemoryEngine.cc \
	src/agent/AipyAdapter.cc \
	src/agent/PersonaManager.cc \
	src/agent/SkillManager.cc \
	src/agent/McpClient.cc \
	src/agent/McpManager.cc \
	src/agent/ShijimaHttpApi.cc \
	src/music/MusicFavoriteDb.cc \
	src/music/MusicApiService.cc \
	src/music/MusicPlayerManager.cc \
	src/music/MusicPlayerDialog.cc \
	src/timer/TimerManager.cc \
	src/timer/TimerListDialog.cc \
	src/system/TrashWatcher.cc \
	src/system/UpdateManager.cc \
	src/system/SensorManager.cc \
	$(PLATFORM_SYSTEM_SOURCES) \
	src/ui/FloatingFileWidget.cc \
	src/ui/TrashTargetWidget.cc \
	src/ui/UpdateDialog.cc \
	src/ui/MessageBubble.cc \
	src/ui/ScoreBadgeWidget.cc \
	src/ui/PetStatusBarWidget.cc \
	src/ui/SelectionToolbar.cc \
	src/ui/ShijimaContextMenu.cc \
	src/ui/AskDialog.cc \
	src/ui/AgentSettingsDialog.cc \
	src/ui/MessageHistoryDialog.cc \
	src/ui/ShimejiInspectorDialog.cc \
	src/ui/ShijimaApiDialog.cc \
	src/ui/ShijimaLicensesDialog.cc \
	src/ui/PetDiaryDialog.cc \
	src/ui/ForcedProgressDialog.cc \
	resources.rc

DEFAULT_MASCOT_FILES := $(addsuffix .png,$(addprefix DefaultMascot/img/shime,$(shell seq -s ' ' 1 1 46))) \
	DefaultMascot/behaviors.xml DefaultMascot/actions.xml

LICENSE_FILES := Shijima-Qt.LICENSE.txt \
	duktape.LICENSE.txt \
	duktape.AUTHORS.rst \
	libarchive.LICENSE.txt \
	libshijima.LICENSE.txt \
	libshimejifinder.LICENSE.txt \
	unarr.LICENSE.txt \
	unarr.AUTHORS.txt \
	Qt.LICENSE.txt \
	rapidxml.LICENSE.txt

LICENSE_FILES := $(addprefix licenses/,$(LICENSE_FILES))

API_DOC_FILES := HTTP-API.md

QT_LIBS = Widgets Core Gui Concurrent Network

TARGET_LDFLAGS += -Llibshimejifinder/build/unarr -lunarr

ifeq ($(PLATFORM),Linux)
QT_LIBS += DBus
PKG_LIBS := x11
TARGET_LDFLAGS += -Wl,-R -Wl,$(shell pwd)/publish/Linux/$(CONFIG) -lsqlite3
endif


ifeq ($(PLATFORM),Windows)
TARGET_LDFLAGS += -lws2_32
endif

ifeq ($(PLATFORM),macOS)
TARGET_LDFLAGS += -L/opt/homebrew/opt/libarchive/lib -larchive -lsqlite3
CXXFLAGS += -I/opt/homebrew/opt/libarchive/include
endif


ifeq ($(SHIJIMA_USE_QTMULTIMEDIA),1)
QT_LIBS += Multimedia
CXXFLAGS += -DSHIJIMA_USE_QTMULTIMEDIA=1
else
CXXFLAGS += -DSHIJIMA_USE_QTMULTIMEDIA=0
endif

CXXFLAGS += -I. -Isrc -Isrc/core -Isrc/pet -Isrc/agent -Isrc/music -Isrc/timer -Isrc/system -Isrc/ui -Ilibshijima -Ilibshimejifinder -Icpp-httplib

PKG_LIBS += libarchive
PUBLISH_DLL = $(addprefix Qt6,$(QT_LIBS))


define download_linuxdeploy
@uname_m="$$(uname -m)"; \
if [ "$${uname_m}" = "$(1)" -o "$${uname_m}" = "$(2)" ]; then \
	url="https://github.com/linuxdeploy/$(3)/releases/latest/download/$(3)-$(2).AppImage"; \
	name="$${url##*/}"; \
	echo "==> $${url}"; \
	wget -O "$${name}" -c --no-verbose "$${url}"; \
	touch "$${name}"; \
	chmod +x "$${name}"; \
	name2="$${name%-$(2).AppImage}.AppImage"; \
	ln -s "$${name}" "$${name2}"; \
fi
endef

TARGET ?= guyi-bot

all:: publish/$(PLATFORM)/$(CONFIG)

publish/Windows/$(CONFIG): $(TARGET)$(EXE) FORCE
	mkdir -p $@
	@$(call copy_changed,libshimejifinder/build/unarr/libunarr.so.1.1.0,$@)
	@$(call copy_changed,$<,$@)
	@$(call copy_exe_dlls,$<,$@)
	@$(call copy_qt_plugin_dlls,$@)
	if [ $(CONFIG) = release ]; then find $@ -name '*.dll' -exec $(STRIP) -S '{}' \;; fi
	if [ $(CONFIG) = release ]; then $(STRIP)  -S $@/libunarr.so.1.1.0; fi

linuxdeploy-plugin-appimage-x86_64.AppImage:
	$(call download_linuxdeploy,x86_64,x86_64,linuxdeploy-plugin-appimage)

linuxdeploy-plugin-qt-x86_64.AppImage:
	$(call download_linuxdeploy,x86_64,x86_64,linuxdeploy-plugin-qt)

linuxdeploy-x86_64.AppImage: linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-appimage-x86_64.AppImage
	$(call download_linuxdeploy,x86_64,x86_64,linuxdeploy)

linuxdeploy-plugin-appimage-aarch64.AppImage:
	$(call download_linuxdeploy,arm64,aarch64,linuxdeploy-plugin-appimage)

linuxdeploy-plugin-qt-aarch64.AppImage:
	$(call download_linuxdeploy,arm64,aarch64,linuxdeploy-plugin-qt)

linuxdeploy-aarch64.AppImage: linuxdeploy-plugin-qt-aarch64.AppImage linuxdeploy-plugin-appimage-aarch64.AppImage
	$(call download_linuxdeploy,arm64,aarch64,linuxdeploy)

linuxdeploy.AppImage: linuxdeploy-aarch64.AppImage linuxdeploy-x86_64.AppImage

publish/macOS/$(CONFIG): $(TARGET)$(EXE)
	mkdir -p $@
	$(call copy_changed,libshimejifinder/build/unarr/libunarr.1.dylib,$@)
	$(call copy_changed,$<,$@)
	if [ $(CONFIG) = release ]; then $(STRIP) -S $@/libunarr.1.dylib; fi
	install_name_tool -add_rpath "@loader_path" $@/$< 2>/dev/null || true
	install_name_tool -add_rpath "$$(realpath $@)" $@/$< 2>/dev/null || true

publish/Linux/$(CONFIG): $(TARGET)$(EXE)
	mkdir -p $@
	@cp -d libshimejifinder/build/unarr/libunarr.so* $@/ 2>/dev/null || true
	if [ $(CONFIG) = release ]; then $(STRIP) -S $@/libunarr.so* 2>/dev/null || true; fi
	@$(call copy_changed,$<,$@)


publish/macOS/$(CONFIG)/$(TARGET).app: publish/macOS/$(CONFIG)
	rm -rf $@ && [ ! -d $@ ]
	if [ -d guyi-bot.app ]; then cp -r guyi-bot.app $@; else cp -r Shijima-Qt.app $@; fi
	mkdir -p $@/Contents/MacOS $@/Contents/Frameworks
	cp $^/$(TARGET) $@/Contents/MacOS/$(TARGET)
	cp libshimejifinder/build/unarr/libunarr.1.dylib $@/Contents/MacOS/ 2>/dev/null || true
	cp libshimejifinder/build/unarr/libunarr.1.dylib $@/Contents/Frameworks/ 2>/dev/null || true
	macdeployqt $@ || true
	codesign --force --deep --sign - $@ 2>/dev/null || true

publish/Linux/$(CONFIG)/$(TARGET).AppImage: publish/Linux/$(CONFIG) linuxdeploy.AppImage
	rm -rf AppDir
	NO_STRIP=1 ./linuxdeploy.AppImage --appdir AppDir --executable publish/Linux/$(CONFIG)/$(TARGET) \
		--desktop-file com.pixelomer.ShijimaQt.desktop --output appimage --plugin qt --icon-file \
		com.pixelomer.ShijimaQt.png
	mv $(TARGET)-*.AppImage $(TARGET).AppImage 2>/dev/null || true
	cp $(TARGET).AppImage publish/Linux/$(CONFIG)/ 2>/dev/null || true

appimage: publish/Linux/$(CONFIG)/$(TARGET).AppImage

macapp: publish/macOS/$(CONFIG)/$(TARGET).app

$(TARGET)$(EXE): $(TARGET).a Platform/Platform.a libshimejifinder/build/libshimejifinder.a \
	libshijima/build/libshijima.a
	$(CXX) -o $@ $(LD_COPY_NEEDED) $(LD_WHOLE_ARCHIVE) $^ $(LD_NO_WHOLE_ARCHIVE) \
		$(TARGET_LDFLAGS) $(LDFLAGS)
	if [ $(CONFIG) = "release" ]; then $(STRIP) $@; fi
	if [ "$$(uname -s)" = "Darwin" ]; then \
		install_name_tool -add_rpath "@loader_path" $@ 2>/dev/null || true; \
		install_name_tool -add_rpath "@loader_path/libshimejifinder/build/unarr" $@ 2>/dev/null || true; \
		cp libshimejifinder/build/unarr/libunarr.1.dylib ./ 2>/dev/null || true; \
	fi
	ln -sf $(TARGET)$(EXE) shijima-qt$(EXE)

libshijima/build/libshijima.a: libshijima/build/Makefile
	$(MAKE) -C libshijima/build

src/core/DefaultMascot.cc: $(DEFAULT_MASCOT_FILES) Makefile bundle-default.sh
	python3 ./bundle-default.sh $(DEFAULT_MASCOT_FILES) > '$@-'
	mv '$@-' '$@'

src/core/DefaultMascot.o: src/core/DefaultMascot.cc

src/ui/ShijimaLicensesDialog.o: licenses_generated.hpp

licenses_generated.hpp: $(LICENSE_FILES) Makefile
	echo 'static const char *shijima_licenses = R"SHIJIMA_LIC(' > licenses_generated.hpp
	echo 'Licenses for the software components used in guyi-bot are listed below.' >> licenses_generated.hpp
	echo '基础项目代码来自：Shijima-Qt ：https://getshijima.app (https://github.com/pixelomer/Shijima-Qt)' >> licenses_generated.hpp
	echo '项目地址 ：https://github.com/guyi57/guyi-bot' >> licenses_generated.hpp
	echo '原项目源地址 ：https://github.com/pixelomer/Shijima-Qt/issues' >> licenses_generated.hpp
	echo '音乐服务出处：GD音乐台 (music.gdstudio.xyz) ：https://music.gdstudio.xyz' >> licenses_generated.hpp
	for file in $^; do \
		[ "$$file" != "Makefile" ] || continue; \
		(echo; echo) >> licenses_generated.hpp; \
		echo "~~~~~~~~~~ $$(basename $$file) ~~~~~~~~~~" >> licenses_generated.hpp; \
		echo >> licenses_generated.hpp; \
		cat $$file >> licenses_generated.hpp; \
	done
	echo ')SHIJIMA_LIC";' >> licenses_generated.hpp


src/ui/ShijimaApiDialog.o: api_doc_generated.hpp

api_doc_generated.hpp: $(API_DOC_FILES) Makefile
	echo 'static const char *shijima_api_doc = R"SHIJIMA_API_DOC(' > api_doc_generated.hpp
	for file in $^; do \
		[ "$$file" != "Makefile" ] || continue; \
		cat $$file >> api_doc_generated.hpp; \
	done
	echo ')SHIJIMA_API_DOC";' >> api_doc_generated.hpp


libshijima/build/Makefile: libshijima/CMakeLists.txt FORCE
	mkdir -p libshijima/build && cd libshijima/build && $(CMAKE) $(CMAKEFLAGS) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="-Wno-error" -DCMAKE_CXX_FLAGS="-DSHIJIMA_DUK_STATIC_BUILD -Wno-error" -DSHIJIMA_BUILD_EXAMPLES=NO ..

libshimejifinder/build/Makefile: libshimejifinder/CMakeLists.txt FORCE
	chmod +x libshimejifinder/bin2cpp.sh 2>/dev/null || true
	python3 -c 'import os; [open(o, "w").write("#include <cstddef>\nstatic const char " + os.path.splitext(os.path.basename(o))[0] + "[] = \n" + "\n".join(["    \"" + "".join(fr"\x{b:02X}" for b in open(i, "rb").read()[j:j+16]) + "\"" for j in range(0, len(open(i, "rb").read()), 16)]) + "\n;\nstatic const size_t " + os.path.splitext(os.path.basename(o))[0] + "_len = " + str(len(open(i, "rb").read())) + ";\n") for i, o in [("libshimejifinder/default_actions.xml", "libshimejifinder/shimejifinder/default_actions.cc"), ("libshimejifinder/default_behaviors.xml", "libshimejifinder/shimejifinder/default_behaviors.cc")]]'
	mkdir -p libshimejifinder/build && cd libshimejifinder/build && \
		PKG_CONFIG_PATH="/opt/homebrew/opt/libarchive/lib/pkgconfig:$$PKG_CONFIG_PATH" \
		$(CMAKE) $(CMAKEFLAGS) \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		-DCMAKE_C_FLAGS="-Wno-error" -DCMAKE_CXX_FLAGS="-Wno-error" \
		-DSHIMEJIFINDER_BUILD_LIBARCHIVE=NO -DSHIMEJIFINDER_BUILD_EXAMPLES=NO ..



libshimejifinder/build/libshimejifinder.a: libshimejifinder/build/Makefile
	$(MAKE) -C libshimejifinder/build
	if [ $(PLATFORM) = "Windows" ]; then cp libshimejifinder/build/unarr/libunarr.so.1.1.0 \
		libshimejifinder/build/unarr/libunarr.dll; fi

clean::
	rm -rf publish/$(PLATFORM)/$(CONFIG) libshijima/build libshimejifinder/build
	rm -f $(OBJECTS) $(TARGET).a shijima-qt.a $(TARGET)$(EXE) shijima-qt$(EXE) $(TARGET).AppImage Shijima-Qt.AppImage
	$(MAKE) -C Platform clean

install:
	install -Dm755 publish/Linux/$(CONFIG)/$(TARGET) $(PREFIX)/bin/$(TARGET)
	install -Dm755 publish/Linux/$(CONFIG)/libunarr.so.1 $(PREFIX)/lib/libunarr.so.1
	install -Dm644 com.pixelomer.ShijimaQt.desktop $(PREFIX)/share/applications/com.pixelomer.ShijimaQt.desktop
	install -Dm644 com.pixelomer.ShijimaQt.metainfo.xml $(PREFIX)/share/metainfo/com.pixelomer.ShijimaQt.metainfo.xml
	install -Dm644 com.pixelomer.ShijimaQt.png $(PREFIX)/share/icons/hicolor/512x512/apps/com.pixelomer.ShijimaQt.png

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(PREFIX)/lib/libunarr.so.1
	rm -f $(PREFIX)/share/applications/com.pixelomer.ShijimaQt.desktop
	rm -f $(PREFIX)/share/metainfo/com.pixelomer.ShijimaQt.metainfo.xml
	rm -f $(PREFIX)/share/icons/hicolor/512x512/apps/com.pixelomer.ShijimaQt.png

Platform/Platform.a: FORCE
	$(MAKE) -C Platform

$(TARGET).a: $(OBJECTS) Makefile
	ar rcs $@ $(filter %.o,$^)

shijima-qt.a: $(TARGET).a
	ln -sf $< $@

.PHONY: install uninstall
