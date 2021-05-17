GCC = g++
CFLAGS = -O2 -std=c++14
SSEFLAGS = -msse2 -mssse3 -msse4.1 -msse4.2 -mavx -march=native
FILES = device.out server.out

all: $(FILES) 

device.out: device/device.cpp
	$(GCC) $(CFLAGS) $(SSEFLAGS) -o device/device.out device/device.cpp -lpcap -lpthread

server.out: server/server.cpp
	$(GCC) $(CFLAGS) $(SSEFLAGS) -o server/server.out server/server.cpp -lpcap -lpthread

common.o:
	$(GCC) $(CFLAGS) -c common/common.cc

os_galoisField.o:
	$(GCC) $(CFLAGS) -c common/os_galoisField.cc

os_mangler.o:
	$(GCC) $(CFLAGS) -c common/os_mangler.cc

clean:
	rm $(all) -f *~ *.o *.out