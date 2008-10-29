all : nicodown

nicodown : nicodown.c
	gcc -g -o nicodown nicodown.c `curl-config --cflags --libs`

	@#gcc -g -o nicodown nicodown.c `curl-config --cflags --libs` `xml2-config --cflags --libs`
