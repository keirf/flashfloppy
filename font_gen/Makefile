#
# shinonome BDF to KANJI ROM MB832001-042 format converter
#
EXE = bdf2mb.exe
OBJ = bdf2mb.o

CC = gcc
LD = gcc
CFLAGS = -O2 -Wall
LIBS   = 

$(EXE):$(OBJ)
	$(LD) $^ $(LIBS) -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@
