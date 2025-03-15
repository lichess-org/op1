mbeval_uci: mbeval.c
	gcc mbeval.c -o mbeval_uci -Wall -Wpedantic -std=c2x -D_XOPEN_SOURCE=500 -O2 -lzstd -lz -fsanitize=undefined -fsanitize=address

.PHONY: format
format:
	clang-format -i mbeval.c
