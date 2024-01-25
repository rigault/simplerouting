show: show.o option.o rutil.o engine.o shputil.o gpsutil.o curlutil.o
	gcc show.o option.o rutil.o engine.o shputil.o gpsutil.o curlutil.o -o show -lcurl -pthread -lgps -lshp -leccodes -lm  -I /usr/include/webkitgtk-4.0 -I/usr/include/libsoup-2.4 -I/usr/include/gtk-3.0 -I/usr/include/at-spi2-atk/2.0 -I/usr/include/at-spi-2.0 -I/usr/include/dbus-1.0 -I/usr/lib/x86_64-linux-gnu/dbus-1.0/include -I/usr/include/gtk-3.0 -I/usr/include/gio-unix-2.0 -I/usr/include/cairo -I/usr/include/pango-1.0 -I/usr/include/harfbuzz -I/usr/include/pango-1.0 -I/usr/include/fribidi -I/usr/include/harfbuzz -I/usr/include/atk-1.0 -I/usr/include/cairo -I/usr/include/pixman-1 -I/usr/include/uuid -I/usr/include/freetype2 -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/libpng16 -I/usr/include/x86_64-linux-gnu -I/usr/include/libmount -I/usr/include/blkid -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lpango-1.0 -lharfbuzz -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0

show.o: show.c rtypes.h option.h rutil.h engine.h shputil.h gpsutil.h curlutil.h
	gcc -c `pkg-config gtk+-3.0 --cflags` show.c `pkg-config gtk+-3.0 --libs` -I/usr/include/webkitgtk-4.0 -I/usr/include/libsoup-2.4

engine.o: engine.c rtypes.h rutil.h
	gcc -Wall -Wextra -c engine.c

rutil.o: rutil.c rtypes.h
	gcc -Wall -c rutil.c -lm

shputil.o: shputil.c rtypes.h
	gcc -Wall -Wextra -c shputil.c -lshp
	
gpsutil.o: gpsutil.c rtypes.h
	gcc -Wall -Wextra -c gpsutil.c -lgps

curlutil.o: curlutil.c
	gcc -Wall -Wextra -c curlutil.c -lcurl

option.o: option.c rtypes.h rutil.h
	gcc -Wall -Wextra -c option.c

clean:
	rm -f *.o
	mv  show../.
