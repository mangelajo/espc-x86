# ESPC-x86 — Top-level Makefile
#
# Run `make help` to list available targets.

PIO ?= pio

.DEFAULT_GOAL := help

.PHONY: help all esp32 native test run flash monitor clean clean-esp32 clean-native

help: ## Show this help
	@awk 'BEGIN {FS = ":.*##"; printf "Usage: make \033[36m<target>\033[0m\n\nTargets:\n"} \
		/^[a-zA-Z0-9_-]+:.*##/ { printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

all: esp32 native ## Build both ESP32 and native

esp32: ## Build the ESP32 firmware
	$(PIO) run -e esp32dev

native: ## Build the native (host) binary
	$(PIO) run -e native

test: ## Build and run native unit tests
	$(PIO) test -e native

run: native ## Build and run the native binary
	$(PIO) run -e native -t exec

flash: ## Build and flash ESP32 firmware
	$(PIO) run -e esp32dev -t upload

monitor: ## Open ESP32 serial monitor
	$(PIO) device monitor

clean: clean-esp32 clean-native ## Clean all build artifacts

clean-esp32: ## Clean ESP32 build only
	$(PIO) run -e esp32dev -t clean 2>/dev/null || true

clean-native: ## Clean native build only
	$(PIO) run -e native -t clean 2>/dev/null || true
