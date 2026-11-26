PROJ ?= proj
PLUGINS := sketchybar/plugins
SHELL_SCRIPTS := $(shell git ls-files -co --exclude-standard -- '*.sh' 'sketchybar/sketchybarrc' 'yabai/yabairc' 'scripts/ui' 'scripts/prove')

.PHONY: build lint test agent stop proofs golden install apply try back deploy dev-guided

## Helpers the bar runs, and the VM's input tool. Built only when the source is newer, so a binary that
## holds a TCC grant (bt_battery: Bluetooth) is not re-signed for nothing.
build: $(PLUGINS)/appearance $(PLUGINS)/bt_battery build/guestinput

$(PLUGINS)/appearance: $(PLUGINS)/appearance.swift
	swiftc -O $< -o $@

$(PLUGINS)/bt_battery: $(PLUGINS)/bt_battery.swift $(PLUGINS)/bt_battery.plist
	swiftc -O $< -o $@ -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker $(PLUGINS)/bt_battery.plist
	codesign -fs - --identifier dev.zilly.sketchybar.bt-battery $@

build/guestinput: tools/guestinput.swift
	@mkdir -p build
	swiftc -O $< -o $@

lint:
	@if command -v $(PROJ) >/dev/null 2>&1; then $(PROJ) check; \
	else echo "lint: $(PROJ) is not installed, skipping repo policy checks"; fi
	@jq empty config.default.json
	@scripts/render-skhdrc.sh --default | diff -u skhd/skhdrc - || { echo "skhd/skhdrc is out of sync with config.default.json; run scripts/render-skhdrc.sh --default -o skhd/skhdrc" >&2; exit 1; }
	@for f in $(SHELL_SCRIPTS); do bash -n "$$f" || exit 1; done
	@if command -v shellcheck >/dev/null; then shellcheck -S error $(SHELL_SCRIPTS); \
	else echo "lint: shellcheck is not installed (brew install shellcheck), so the scripts were only parsed"; fi

test: lint build
	$(MAKE) -C spacesd test
	scripts/live_test.sh

## The hidden agent copy: this checkout in its own headless VM (docs/agent-vm.md).
agent: build
	scripts/agent-app.sh start

stop:
	scripts/agent-app.sh stop

proofs: build
	scripts/prove

## Guided mode: this checkout's agent VM in a window, so the user tries a branch without switching their
## desktop while the agent watches through scripts/ui. The user's to run.
dev-guided: build
	@scripts/agent-app.sh stop >/dev/null
	scripts/agent-app.sh start --window

golden:
	scripts/vm/provision-golden.sh

## The live desktop runs the main checkout: try a branch on it, come back, or apply main after a merge
## (scripts/live.sh). These change what the user is looking at, so they are the user's to run.
try:
	@scripts/live.sh try "$(BRANCH)"

back deploy:
	@scripts/live.sh $@

## Apply ~/.config/kuukan/config.json changes (regenerates skhdrc and reloads services).
apply:
	@scripts/render-skhdrc.sh -o "$$HOME/.config/kuukan/skhdrc"
	@yabai --restart-service
	@sketchybar --reload
	@skhd --reload
	@"$$HOME/.config/kuukan/spacesd/bin/spacesd" -m repaint >/dev/null 2>&1 || true

## Link ~/.config to this checkout, initialize ~/.config/kuukan/config.json, and render the LaunchAgent plist.
install:
	@mkdir -p "$$HOME/.config/kuukan" "$$HOME/Library/LaunchAgents"
	@for x in sketchybar yabai; do \
	  t="$(CURDIR)/$$x"; l="$$HOME/.config/$$x"; \
	  if [ -L "$$l" ] && [ "$$(readlink "$$l")" = "$$t" ]; then echo "ok $$l"; \
	  elif [ -e "$$l" ] || [ -L "$$l" ]; then echo "$$l exists and is not a link to $$t; move it aside first" >&2; exit 1; \
	  else ln -s "$$t" "$$l" && echo "linked $$l -> $$t"; fi; \
	done
	@ln -sfn "$(CURDIR)/spacesd" "$$HOME/.config/kuukan/spacesd" && echo "linked $$HOME/.config/kuukan/spacesd -> $(CURDIR)/spacesd"
	@ln -sfn "$(CURDIR)/config.default.json" "$$HOME/.config/kuukan/config.default.json" && echo "linked $$HOME/.config/kuukan/config.default.json"
	@if [ ! -f "$$HOME/.config/kuukan/config.json" ]; then \
	  cp "$(CURDIR)/config.default.json" "$$HOME/.config/kuukan/config.json" && echo "created $$HOME/.config/kuukan/config.json"; \
	else echo "ok $$HOME/.config/kuukan/config.json (kept existing)"; fi
	@scripts/render-skhdrc.sh -o "$$HOME/.config/kuukan/skhdrc" && echo "rendered $$HOME/.config/kuukan/skhdrc"
	@if [ -L "$$HOME/.config/skhd" ]; then \
	  if [ "$$(readlink "$$HOME/.config/skhd")" = "$(CURDIR)/skhd" ]; then \
	    rm "$$HOME/.config/skhd" && mkdir -p "$$HOME/.config/skhd" && ln -s "$$HOME/.config/kuukan/skhdrc" "$$HOME/.config/skhd/skhdrc" && echo "linked $$HOME/.config/skhd/skhdrc -> $$HOME/.config/kuukan/skhdrc"; \
	  else echo "$$HOME/.config/skhd is a symlink to $$(readlink "$$HOME/.config/skhd"); move it aside first" >&2; exit 1; fi; \
	else \
	  mkdir -p "$$HOME/.config/skhd"; \
	  l="$$HOME/.config/skhd/skhdrc"; t="$$HOME/.config/kuukan/skhdrc"; \
	  if [ -L "$$l" ] && [ "$$(readlink "$$l")" = "$$t" ]; then echo "ok $$l"; \
	  elif [ -e "$$l" ] || [ -L "$$l" ]; then echo "$$l exists and is not a link to $$t; move it aside first" >&2; exit 1; \
	  else ln -s "$$t" "$$l" && echo "linked $$l -> $$t"; fi; \
	fi
	@plist="$$HOME/Library/LaunchAgents/dev.zilly.spacesd.plist"; \
	sed "s|@SPACESD_BIN@|$(CURDIR)/spacesd/bin/spacesd|g" "$(CURDIR)/spacesd/launchd/dev.zilly.spacesd.plist.in" > "$$plist" && echo "rendered $$plist"
