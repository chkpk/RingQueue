.PHONY:clean all

all : test

test : main.cpp RingQueue.h
	g++ -O3 -o test main.cpp -lpthread  -Wall -Wextra -Werror -Wconversion -Wshadow

clean : 
	rm test 
