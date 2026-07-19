.PHONY: clean build build-wasm run-mac run-wasm

BUILD_DIR := build
WEB_DIR := build-web
PROJECT := hex-magical
BINARY := $(BUILD_DIR)/$(PROJECT)/$(PROJECT)

WASM_BUILD_DIR := bin/wasm
ITCH_DIR := hex-magical
ITCH_ZIP := hex-magical.zip

clean:
	rm -rf $(BUILD_DIR) $(WEB_DIR) $(WASM_BUILD_DIR) $(ITCH_DIR) $(ITCH_ZIP)
	$(MAKE) -f Makefile.web clean

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

build-wasm:
	$(MAKE) -f Makefile.web all
	rm -rf $(ITCH_DIR)
	mkdir -p $(ITCH_DIR)
	cp $(WASM_BUILD_DIR)/index.html $(WASM_BUILD_DIR)/index.js $(WASM_BUILD_DIR)/index.wasm $(WASM_BUILD_DIR)/index.data $(ITCH_DIR)/
	rm -f $(ITCH_ZIP)
	cd $(ITCH_DIR) && zip -r ../$(ITCH_ZIP) .
	@echo "Itch package ready: $(ITCH_ZIP)"

run-mac: build
	./$(BINARY)

run-wasm: build-wasm
	@echo "Serving at http://localhost:8000/"
	cd $(ITCH_DIR) && python3 -m http.server 8000
