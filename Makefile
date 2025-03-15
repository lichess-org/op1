mbeval_uci: mbeval.c
	gcc mbeval.c -o mbeval_uci -Wall -O2 -lzstd -lz

.PHONY: format
format:
	clang-format -i mbeval.c
