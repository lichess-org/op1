mbeval.o: src/mbeval.c include/mbeval.h
	gcc -c src/mbeval.c -Iinclude -Wall -Wpedantic -std=c2x -D_XOPEN_SOURCE=500 -O2 -lzstd -lz -fsanitize=undefined -fsanitize=address

.PHONY: format
format:
	clang-format -i src/mbeval.c include/mbeval.h
