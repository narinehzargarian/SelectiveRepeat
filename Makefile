USERID=123456789

default: build

build: server.cpp client.cpp
	g++ -Wall -Wextra -o server server.cpp
	g++ -Wall -Wextra -o client client.cpp

clean:
	rm -rf *.o server client *.tar.gz

dist: zip
zip: clean
	zip ${USERID}.zip server.cpp client.cpp Makefile