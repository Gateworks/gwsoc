CFLAGS += -Wall -Wno-pointer-sign

CFLAGS += -I cybootloaderutils
CYAPI = cybootloaderutils/cybtldr_api2.c \
	cybootloaderutils/cybtldr_api.c \
	cybootloaderutils/cybtldr_command.c \
	cybootloaderutils/cybtldr_parse.c

LDFLAGS:=-lhidapi-libusb

all: gwsoc

gwsoc: gwsoc.c program.c $(CYAPI)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f *.o gwsoc
