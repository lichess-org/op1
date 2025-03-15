mbeval_uci: mbeval.c
	gcc mbeval.c -o mbeval_uci -Wall -Wpedantic -O2 -lzstd -lz -fsanitize=undefined -fsanitize=address

.PHONY: format
format:
	clang-format -i mbeval.c
