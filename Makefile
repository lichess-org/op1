mbeval_uci: mbeval.c
	gcc mbeval.c -o mbeval_uci -O2 -Wfatal-errors -DUCI -lzstd -lz
