all: server client

server: server.c
	gcc -Wall server.c -o server

client: client.c
	gcc -Wall client.c -o client