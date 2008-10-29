all : nicodown

nicodown : nicodown.c
	gcc -o nicodown nicodown.c `curl-config --cflags --libs`
