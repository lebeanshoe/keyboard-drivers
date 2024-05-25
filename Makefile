EXE = driver
OBJS = driver.o

CXX = g++
CXXFLAGS = -std=c++11 -c -Wall -I/usr/include/libusb-1.0
LD = g++
LDFLAGS = -std=c++11 -lusb-1.0

output: $(OBJS)
	$(LD) $(OBJS) $(LDFLAGS) -o $(EXE)

driver.o: driver.cpp
	$(CXX) $(CXXFLAGS) driver.cpp

clean:
	rm *.o $(EXE)