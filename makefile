.PHONY: clean build build-wasm run-mac run-wasm package-wasm app-bundle package-macos package-windows \
	help ci ci-watch release release-watch test cook-content

BUILD_DIR := build
PROJECT := hex-magical
BINARY := $(BUILD_DIR)/$(PROJECT)/$(PROJECT)
# Multi-config generators (VS) put the binary under Release/
BINARY_RELEASE := $(BUILD_DIR)/$(PROJECT)/Release/$(PROJECT)

WASM_BUILD_DIR := bin/wasm
ITCH_DIR := hex-magical
ITCH_ZIP := hex-magical.zip

# Cooked ship tree (dependency-trimmed). Dev builds still use resources/.
CONTENT_DIR := dist/content
COOK_BIN := tools/cook-content/cook-content

APP_NAME := hex-magical
APP_BUNDLE := $(APP_NAME).app
CONTENTS_DIR := $(APP_BUNDLE)/Contents
MACOS_DIR := $(CONTENTS_DIR)/MacOS
BUNDLE_RESOURCES_DIR := $(CONTENTS_DIR)/Resources

# CI / release dispatch (requires gh auth)
HEX_MAGICAL_WORKFLOW := .github/workflows/hex-magical-cicd.yml
PLATFORM ?= all
REF ?= $(shell git branch --show-current 2>/dev/null)
LEVEL_TESTS ?= false
# Optional overrides: VERSION=0.0.2 CHANNEL=web LEVEL_TESTS=true

clean:
	rm -rf $(BUILD_DIR) $(WASM_BUILD_DIR) $(ITCH_DIR) $(ITCH_ZIP) \
		$(APP_BUNDLE) hex-magical-macos-arm64.zip hex-magical-windows-x86_64.zip release \
		$(CONTENT_DIR) $(COOK_BIN)
	$(MAKE) -f Makefile.web clean

# Fast C cook: playable maps + tileset/image deps + celestial sprites + solutions.
cook-content: $(COOK_BIN)
	$(COOK_BIN) resources $(CONTENT_DIR)

$(COOK_BIN): tools/cook-content/main.c
	$(CC) -O2 -std=c17 -Wall -Wextra -o $@ $<

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --config Release -j

build-wasm: cook-content
	$(MAKE) -f Makefile.web all RESOURCES_DIR=$(CONTENT_DIR)
	rm -rf $(ITCH_DIR)
	mkdir -p $(ITCH_DIR)
	cp $(WASM_BUILD_DIR)/index.html $(WASM_BUILD_DIR)/index.js $(WASM_BUILD_DIR)/index.wasm $(WASM_BUILD_DIR)/index.data $(ITCH_DIR)/
	rm -f $(ITCH_ZIP)
	cd $(ITCH_DIR) && zip -r ../$(ITCH_ZIP) .
	@echo "Itch package ready: $(ITCH_ZIP)"

package-wasm: cook-content
	$(MAKE) -f Makefile.web package RESOURCES_DIR=$(CONTENT_DIR)

# Resolve desktop binary path (single-config vs multi-config)
define resolve_binary
$(shell if [ -f "$(BINARY)" ]; then echo "$(BINARY)"; \
	elif [ -f "$(BINARY_RELEASE)" ]; then echo "$(BINARY_RELEASE)"; \
	elif [ -f "$(BINARY).exe" ]; then echo "$(BINARY).exe"; \
	elif [ -f "$(BINARY_RELEASE).exe" ]; then echo "$(BINARY_RELEASE).exe"; \
	else echo ""; fi)
endef

app-bundle: build cook-content
	@EXE="$(resolve_binary)"; \
	if [ -z "$$EXE" ]; then echo "Error: binary not found after build"; exit 1; fi; \
	echo "==== Creating macOS .app from $$EXE ===="; \
	rm -rf "$(APP_BUNDLE)"; \
	mkdir -p "$(MACOS_DIR)" "$(BUNDLE_RESOURCES_DIR)"; \
	cp "$$EXE" "$(MACOS_DIR)/$(APP_NAME)"; \
	chmod +x "$(MACOS_DIR)/$(APP_NAME)"; \
	cp src/platform/Info.plist "$(CONTENTS_DIR)/Info.plist"; \
	if [ -f src/platform/raylib.icns ]; then cp src/platform/raylib.icns "$(BUNDLE_RESOURCES_DIR)/"; fi; \
	# Next to the binary — GetApplicationDirectory()+resources (no Contents/resources duplicate).
	cp -R "$(CONTENT_DIR)" "$(MACOS_DIR)/resources"; \
	echo "✅ Created $(APP_BUNDLE)"

package-macos: app-bundle
	rm -f hex-magical-macos-arm64.zip
	zip -r hex-magical-macos-arm64.zip $(APP_BUNDLE)
	@echo "✅ Package ready: hex-magical-macos-arm64.zip"

package-windows: build cook-content
	@EXE="$(resolve_binary)"; \
	if [ -z "$$EXE" ]; then echo "Error: binary not found after build"; exit 1; fi; \
	rm -rf release; \
	mkdir -p release; \
	cp "$$EXE" release/hex-magical.exe 2>/dev/null || cp "$$EXE" release/hex-magical; \
	if [ ! -d "$(CONTENT_DIR)" ]; then echo "Error: $(CONTENT_DIR) missing — run make cook-content"; exit 1; fi; \
	cp -R "$(CONTENT_DIR)" release/resources; \
	if [ ! -f release/resources/act-1/map-2.tmx ]; then \
		echo "Error: missing release/resources/act-1/map-2.tmx"; exit 1; \
	fi; \
	if [ ! -f release/resources/solutions/map-2.solution ]; then \
		echo "Error: missing release/resources/solutions/map-2.solution"; exit 1; \
	fi; \
	rm -f hex-magical-windows-x86_64.zip; \
	cd release && zip -r ../hex-magical-windows-x86_64.zip .; \
	echo "✅ Package ready: hex-magical-windows-x86_64.zip"

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
	@echo "  cook-content  - Trim resources/ → dist/content (C tool, for packages)"
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
	@echo "  make hex-magical-ci LEVEL_TESTS=true"

# Dispatch the CI workflow. Examples:
#   make ci
#   make ci PLATFORM=macos
#   make ci PLATFORM=windows VERSION=0.0.2
#   make hex-magical-ci LEVEL_TESTS=true   (opt in to headless level-solution tests)
# Requires: gh (https://cli.github.com/), authenticated (`gh auth login`).
# LEVEL_TESTS defaults to false; pass true to enable run_level_tests.
hex-magical-ci:
	gh workflow run "$(HEX_MAGICAL_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f build_platform="$(PLATFORM)" \
		$(if $(VERSION),-f version="$(VERSION)",) \
		$(if $(CHANNEL),-f channel="$(CHANNEL)",) \
		-f run_level_tests="$(LEVEL_TESTS)"

# Full release: build all platforms, publish itch.io + S3 + GitHub Release.
# Creates git tag v<VERSION> from project.conf (or VERSION= override) via softprops/action-gh-release.
# Examples:
#   make release
#   make release REF=main
#   make release VERSION=0.0.2
#   make hex-magical-release LEVEL_TESTS=true   (release with level-solution tests)
hex-magical-release:
	@V=$${VERSION:-$$(grep '^VERSION=' project.conf | cut -d= -f2)}; \
		echo "🚀 Dispatching release for v$${V} (publish_gh_release=true → creates git tag)"; \
	gh workflow run "$(HEX_MAGICAL_WORKFLOW)" \
		$(if $(REF),-r "$(REF)",) \
		-f build_platform=all \
		-f publish_gh_release=true \
		$(if $(VERSION),-f version="$(VERSION)",) \
		$(if $(CHANNEL),-f channel="$(CHANNEL)",) \
		-f run_level_tests="$(LEVEL_TESTS)"

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
