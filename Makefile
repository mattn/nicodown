ifdef USE_LIBXML
XML_FLAGS=`curl-config --cflags --libs` `xml2-config --cflags --libs`
else
XML_FLAGS=`curl-config --cflags --libs`
endif

FLAGS=$(XML_FLAGS) $(CURSES_FLAGS)

all : nicodown

nicodown : nicodown.c
	gcc -g -o nicodown nicodown.c $(FLAGS)
