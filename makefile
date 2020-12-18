CC := gcc
CFLAGS := $(CFLAGS) -Wall -Wextra -Wc++-compat -Wno-cast-function-type

LOG := echo

all: libpl2w.dll pl2w.exe

examples:
	echo "no examples available for this platform"

pl2w.exe: main.o libpl2w.dll
	@$(LOG) LINK pl2w.exe
	@$(CC) main.o -L. -lpl2w -o pl2w.exe

main.o: pl2w.h main.c
	@$(LOG) CC pl2w.c
	@$(CC) $(CFLAGS) main.c -c -fPIC -o main.o

libpl2w.dll: pl2w.o
	@$(LOG) LINK libpl2w.dll
	@$(CC) pl2w.o -shared -o libpl2w.dll

pl2w.o: pl2w.c pl2w.h
	@$(LOG) CC pl2w.c
	@$(CC) $(CFLAGS) pl2w.c -c -fPIC -o pl2w.o

.PHONY: clean

clean:
	@$(LOG) RM *.o
	@del /F /S /Q *.o
	@$(LOG) RM *.a
	@del /F /S /Q *.a
	@$(LOG) RM *.lib
	@del /F /S /Q *.lib
	@$(LOG) RM *.dll
	@del /F /S /Q *.dll
	@$(LOG) RM *.exe
	@del /F /S /Q *.exe
