CC = gcc
CFLAGS = -g -O2
OBJECTS = generateurls.o

generateurls : $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o generateurls

%.o : %.c
	$(CC) $(CFLAGS) -c $<
