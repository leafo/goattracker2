all: ../example1.prg ../example2.prg ../example3.prg ../example4.prg

../example1.prg: example1.s example1.bin
		dasm example1.s -o../example1.prg -v3 -p3
		pucrunch ../example1.prg ../example1.prg -c64 -x2048

../example2.prg: example2.s example2.bin
		dasm example2.s -o../example2.prg -v3 -p3
		pucrunch ../example2.prg ../example2.prg -c64 -x2048

../example3.prg: example3.s example3.bin
		dasm example3.s -o../example3.prg -v3 -p3
		pucrunch ../example3.prg ../example3.prg -c64 -x2048

../example4.prg: example4.s example4.bin
		dasm example4.s -o../example4.prg -v3 -p3
		pucrunch ../example4.prg ../example4.prg -c64 -x49152
