.PHONY: clean build build-wasm run-mac run-wasm package-wasm app-bundle package-macos package-windows \
	help ci ci-watch release release-watch test

BUILD_DIR := build
PROJECT := hex-magical
BINARY := $(BUILD_DIR)/$(PROJECT)/$(PROJECT)
# Multi-config generators (VS) put the binary under Release/
BINARY_RELEASE := $(BUILD_DIR)/$(PROJECT)/Release/$(PROJECT)

WASM_BUILD_DIR := bin/wasm
ITCH_DIR := hex-magical
ITCH_ZIP := hex-magical.zip

APP_NAME := hex-magical
APP_BUNDLE := $(APP_NAME).app
CONTENTS_DIR := $(APP_BUNDLE)/Contents
MACOS_DIR := $(CONTENTS_DIR)/MacOS
BUNDLE_RESOURCES_DIR := $(CONTENTS_DIR)/Resources

# CI / release dispatch (requires gh auth)
HEX_MAGICAL_WORKFLOW := .github/workflows/hex-magical-cicd.yml
PLATFORM ?= all
REF ?= $(shell git branch --show-current 2>/dev/null)
LEVEL_TESTS ?= true
# Optional overrides: VERSION=0.0.2 CHANNEL=web LEVEL_TESTS=false

clean:
	rm -rf $(BUILD_DIR) $(WASM_BUILD_DIR) $(ITCH_DIR) $(ITCH_ZIP) \
		$(APP_BUNDLE) hex-magical-macos.zip hex-magical-windows.zip release
	$(MAKE) -f Makefile.web clean

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --config Release -j

build-wasm:
	$(MAKE) -f Makefile.web all
	rm -rf $(ITCH_DIR)
	mkdir -p $(ITCH_DIR)
	cp $(WASM_BUILD_DIR)/index.html $(WASM_BUILD_DIR)/index.js $(WASM_BUILD_DIR)/index.wasm $(WASM_BUILD_DIR)/index.data $(ITCH_DIR)/
	rm -f $(ITCH_ZIP)
	cd $(ITCH_DIR) && zip -r ../$(ITCH_ZIP) .
	@echo "Itch package ready: $(ITCH_ZIP)"

package-wasm:
	$(MAKE) -f Makefile.web package

# Resolve desktop binary path (single-config vs multi-config)
define resolve_binary
$(shell if [ -f "$(BINARY)" ]; then echo "$(BINARY)"; \
	elif [ -f "$(BINARY_RELEASE)" ]; then echo "$(BINARY_RELEASE)"; \
	elif [ -f "$(BINARY).exe" ]; then echo "$(BINARY).exe"; \
	elif [ -f "$(BINARY_RELEASE).exe" ]; then echo "$(BINARY_RELEASE).exe"; \
	else echo ""; fi)
endef

app-bundle: build
	@EXE="$(resolve_binary)"; \
	if [ -z "$$EXE" ]; then echo "Error: binary not found after build"; exit 1; fi; \
	echo "==== Creating macOS .app from $$EXE ===="; \
	rm -rf "$(APP_BUNDLE)"; \
	mkdir -p "$(MACOS_DIR)" "$(BUNDLE_RESOURCES_DIR)"; \
	cp "$$EXE" "$(MACOS_DIR)/$(APP_NAME)"; \
	chmod +x "$(MACOS_DIR)/$(APP_NAME)"; \
	cp src/platform/Info.plist "$(CONTENTS_DIR)/Info.plist"; \
	if [ -f src/platform/raylib.icns ]; then cp src/platform/raylib.icns "$(BUNDLE_RESOURCES_DIR)/"; fi; \
	if [ -d resources ]; then \
		cp -R resources "$(CONTENTS_DIR)/resources"; \
		cp -R resources "$(MACOS_DIR)/resources"; \
	fi; \
	echo "✅ Created $(APP_BUNDLE)"

package-macos: app-bundle
	rm -f hex-magical-macos.zip
	zip -r hex-magical-macos.zip $(APP_BUNDLE)
	@echo "✅ Package ready: hex-magical-macos.zip"

package-windows: build
	@EXE="$(resolve_binary)"; \
	if [ -z "$$EXE" ]; then echo "Error: binary not found after build"; exit 1; fi; \
	rm -rf release; \
	mkdir -p release; \
	cp "$$EXE" release/hex-magical.exe 2>/dev/null || cp "$$EXE" release/hex-magical; \
	if [ -d resources ]; then cp -R resources release/resources; fi; \
	rm -f hex-magical-windows.zip; \
	cd release && zip -r ../hex-magical-windows.zip .; \
	echo "✅ Package ready: hex-magical-windows.zip"

run-mac: build
	@EXE="$(resolve_binary)"; \
	if [ -z "$$EXE" ]; then echo "Error: binary not found"; exit 1; fi; \
	./"$$EXE"

# Headless level solution tests: replay resources/solutions/*.solution and
# assert each still solves its level. Fast (pure Box2D stepping, no window).
test: build
	@T="$(BUILD_DIR)/$(PROJECT)/level-tests"; \
	if [ ! -x "$$T" ]; then T="$(BUILD_DIR)/$(PROJECT)/Release/level-tests"; fi; \
	if [ ! -x "$$T" ]; then echo "Error: level-tests binary not found after build"; exit 1; fi; \
	./"$$T"

run-wasm: build-wasm
	@echo "Serving at http://localhost:8000/"
	cd $(ITCH_DIR) && python3 -m http.server 8000

help:
	@echo "hex-magical targets:"
	@echo "  build / run-mac / build-wasm / run-wasm"
	@echo "  test          - Replay saved solutions headlessly (level tests)"
	@echo "  package-wasm / package-macos / package-windows / app-bundle"
	@echo "  ci            - Dispatch CI (PLATFORM, VERSION, CHANNEL, REF)"
	@echo "  ci-watch      - Dispatch CI and watch the run"
	@echo "  release       - Full release: all platforms, itch + S3 + GitHub Release (creates tag from project.conf)"
	@echo "  release-watch - Dispatch release and watch the run"
	@echo ""
	@echo "CI: PLATFORM=all|web|macos|windows  REF=branch  VERSION=  CHANNEL=  LEVEL_TESTS=true|false"
	@echo "Examples:"
	@echo "  make release"
	@echo "  make release REF=main"
	@echo "  make ci PLATFORM=web"
	@echo "  make hex-magical-ci LEVEL_TESTS=false"

# Dispatch the CI workflow. Examples:
#   make ci
#   make ci PLATFORM=macos
#   make ci PLATFORM=windows VERSION=0.0.2
#   make hex-magical-ci LEVEL_TESTS=false   (skip the headless level-solution tests)
# Requires: gh (https://cli.github.com/), authenticated (`gh auth login`).
# run_level_tests is only sent when disabling it, so dispatching a REF branch
# that predates the input still works.
hex-magical-ci:
	gh workflow run "$(HEX_MAGICAL_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f build_platform="$(PLATFORM)" \
		$(if $(VERSION),-f version="$(VERSION)",) \
		$(if $(CHANNEL),-f channel="$(CHANNEL)",) \
		$(if $(filter false,$(LEVEL_TESTS)),-f run_level_tests=false,)

# Full release: build all platforms, publish itch.io + S3 + GitHub Release.
# Creates git tag v<VERSION> from project.conf (or VERSION= override) via softprops/action-gh-release.
# Examples:
#   make release
#   make release REF=main
#   make release VERSION=0.0.2
hex-magical-release:
	@V=$${VERSION:-$$(grep '^VERSION=' project.conf | cut -d= -f2)}; \
		echo "🚀 Dispatching release for v$${V} (publish_gh_release=true → creates git tag)"; \
	gh workflow run "$(HEX_MAGICAL_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f build_platform=all \
		-f publish_gh_release=true \
		$(if $(VERSION),-f version="$(VERSION)",) \
		$(if $(CHANNEL),-f channel="$(CHANNEL)",)

hex-magical-ci-watch: ci
	@sleep 2
	@RID=$$(gh run list --workflow="$(HEX_MAGICAL_WORKFLOW)" -L 1 --json databaseId -q '.[0].databaseId'); \
		test -n "$$RID"; \
		gh run watch "$$RID"

hex-magical-release-watch: release
	@sleep 2
	@RID=$$(gh run list --workflow="$(HEX_MAGICAL_WORKFLOW)" -L 1 --json databaseId -q '.[0].databaseId'); \
		test -n "$$RID"; \
		gh run watch "$$RID"
