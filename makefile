CC = g++ -std=c++11
CXXFLAGS = -std=c++11
all: client server

client: client.cpp packet.cpp packet.h

server: server.cpp packet.cpp packet.h

clean:
		\rm client server
