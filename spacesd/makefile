CFLAGS = -std=c99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fblocks
FRAMEWORKS = -framework CoreFoundation -framework CoreGraphics -framework AppKit -framework Carbon \
             -F/System/Library/PrivateFrameworks -framework SkyLight
C_SRC = src/spacesd.c src/model.c src/state.c src/render.c src/mirror.c src/actions.c \
        src/skylight.c src/bar.c src/yabai_ipc.c src/ccm.c src/papp.c src/config.c
TEST_SRC = tests/test.c src/model.c src/bar.c src/yabai_ipc.c src/ccm.c src/papp.c src/config.c

# A certificate-signed binary keeps its Accessibility grant across rebuilds; an ad-hoc one is keyed
# by content hash. scripts/setup-codesign.sh creates the cert once.
SIGN_ID := $(shell security find-certificate -c spacesd-selfsign -Z ~/Library/Keychains/login.keychain-db 2>/dev/null | awk '/SHA-1/{print $$3; exit}')

# The binary launchd runs: building that one deploys, so it takes DEPLOY=1, which scripts/live.sh passes.
LIVE_BIN := $(shell sed -n 's:.*<string>\(/.*/bin/spacesd\)</string>.*:\1:p' ~/Library/LaunchAgents/dev.zilly.spacesd.plist 2>/dev/null)

bin/spacesd: $(C_SRC) src/workspace.m src/*.h vendor/jsmn.h
	@if [ "$(realpath $(CURDIR))/bin/spacesd" = "$(LIVE_BIN)" ] && [ -z "$(DEPLOY)" ]; then \
	  echo "spacesd: launchd runs this binary, so building it here deploys. Build a branch in its worktree" >&2; \
	  echo "(scripts/agent-app.sh does), and deploy with make try BRANCH=<branch>, back or deploy at the repo root." >&2; exit 1; fi
	@mkdir -p bin
	clang $(CFLAGS) -x objective-c -fobjc-arc -c src/workspace.m -o bin/workspace.o
	clang $(CFLAGS) $(C_SRC) bin/workspace.o -o $@ $(FRAMEWORKS)
	@if [ -n "$(SIGN_ID)" ]; then codesign -s $(SIGN_ID) --force --identifier spacesd $@ && echo "signed: spacesd-selfsign"; else echo "WARN: spacesd-selfsign cert missing, left ad-hoc (Accessibility grant will not persist)"; fi

.PHONY: check test clean

# Every daemon source, warnings as errors, with no output file: safe where bin/spacesd is the live binary.
check:
	clang $(CFLAGS) -fsyntax-only $(C_SRC)
	clang $(CFLAGS) -x objective-c -fobjc-arc -fsyntax-only src/workspace.m

test: check $(TEST_SRC)
	@mkdir -p bin
	clang $(CFLAGS) $(TEST_SRC) -o bin/test
	./bin/test

clean:
	rm -rf bin
