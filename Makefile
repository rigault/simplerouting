routing: show4.o option.o rutil.o engine.o aisgps.o
	gcc show4.o option.o rutil.o engine.o aisgps.o -o routing -std=c11 -lcurl -lshp -leccodes -lm `pkg-config --cflags --libs gtk4`

show4.o: show4.c rtypes.h option.h rutil.h engine.h aisgps.h
	gcc -c show4.c `pkg-config --cflags gtk4`

engine.o: engine.c rtypes.h rutil.h
	gcc -c engine.c `pkg-config --cflags glib-2.0`

rutil.o: rutil.c rtypes.h
	gcc -c rutil.c `pkg-config --cflags glib-2.0`

aisgps.o: aisgps.c rtypes.h
	gcc -c aisgps.c `pkg-config --cflags glib-2.0`

option.o: option.c rtypes.h rutil.h
	gcc -c option.c `pkg-config --cflags glib-2.0`

clean:
	rm -f *.o
	mv  routing ../.
