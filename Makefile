routing: show4.o option.o rutil.o grib.o polar.o engine.o aisgps.o mailutil.o editor.o dashboardVR.o displaytext.o rshputil.o
	gcc show4.o option.o rutil.o grib.o polar.o engine.o aisgps.o mailutil.o editor.o dashboardVR.o displaytext.o rshputil.o -o routing -std=c11 -lcurl -lshp -leccodes -lm `pkg-config --cflags --libs gtk4 gtksourceview-5`

show4.o: show4.c rtypes.h option.h rutil.h grib.h polar.h engine.h aisgps.h mailutil.h editor.h dashboardVR.h
	gcc -c show4.c `pkg-config --cflags gtk4`

engine.o: engine.c rtypes.h rutil.h inline.h
	gcc -c engine.c `pkg-config --cflags glib-2.0`

rutil.o: rutil.c rtypes.h mailutil.h inline.h
	gcc -c rutil.c `pkg-config --cflags glib-2.0`

grib.o: grib.c rtypes.h rutil.h inline.h
	gcc -c grib.c `pkg-config --cflags glib-2.0`

polar.o: polar.c rtypes.h rutil.h inline.h
	gcc -c polar.c `pkg-config --cflags glib-2.0`

aisgps.o: aisgps.c rtypes.h rutil.h
	gcc -c aisgps.c `pkg-config --cflags glib-2.0`

option.o: option.c rtypes.h rutil.h engine.h inline.h
	gcc -c option.c `pkg-config --cflags glib-2.0`

mailutil.o: mailutil.c rtypes.h
	gcc -c mailutil.c `pkg-config --cflags glib-2.0`

editor.o: editor.c
	gcc -c editor.c `pkg-config --cflags gtk4 gtksourceview-5`

dashboardVR.o: dashboardVR.c rtypes.h rutil.h
	gcc -c dashboardVR.c `pkg-config --cflags gtk4`

displaytext.o: displaytext.c rtypes.h
	gcc -c displaytext.c `pkg-config --cflags gtk4`

rshputil.o: rshputil.c rtypes.h
	gcc -c rshputil.c

clean:
	rm -f *.o
	mv  routing ../.
