mbeval_uci: mbeval.cpp
	g++ mbeval.cpp -o mbeval_uci -O2 -Wfatal-errors -DUCI -lzstd -lz
