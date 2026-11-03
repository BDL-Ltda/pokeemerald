#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp /tmp/coop-presence.XXXXXX)
mailbox_binary=$(mktemp /tmp/coop-mailbox.XXXXXX)
link_binary=$(mktemp /tmp/coop-link.XXXXXX)
trap 'rm -f "$test_binary" "$mailbox_binary" "$link_binary"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -iquote tests/coop/stubs -iquote include \
    src/coop/mailbox.c src/coop/presence.c src/coop/overworld.c \
    tests/coop/presence_test.c -o "$test_binary"
"$test_binary"
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -iquote tests/coop/stubs -iquote include \
    src/coop/mailbox.c tests/coop/mailbox_test.c -o "$mailbox_binary"
"$mailbox_binary"
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -iquote tests/coop/link_stubs -iquote include \
    tests/coop/virtual_link_test.c -o "$link_binary"
"$link_binary"
