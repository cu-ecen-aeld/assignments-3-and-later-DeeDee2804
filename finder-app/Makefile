CROSS_COMPILE=aarch64-none-linux-gnu-gcc

hostmake: writer.c
	gcc -o  writer writer.c

clean:
	rm -f writer

arm-make:
	$(CROSS_COMPILE) -o writer writer.c