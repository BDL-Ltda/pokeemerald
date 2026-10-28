#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_binary=$(mktemp /tmp/coop-presence.XXXXXX)
mailbox_binary=$(mktemp /tmp/coop-mailbox.XXXXXX)
spike_binary=$(mktemp /tmp/coop-spike.XXXXXX)
trap 'rm -f "$test_binary" "$mailbox_binary" "$spike_binary"' EXIT HUP INT TERM
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
    -iquote tests/coop/spike_stubs -iquote include \
    tests/coop/link_spike_test.c -o "$spike_binary"
"$spike_binary"
