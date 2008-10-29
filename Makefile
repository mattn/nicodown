all : nicodown

nicodown : nicodown.c
	gcc -g -o nicodown nicodown.c `curl-config --cflags --libs`
