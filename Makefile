all: simplecowfs holdopenfs demotetoregularfilefs

simplecowfs: simplecowfs.c simplecow.c simplecow.h
	${CC} ${CFLAGS} -Wall `pkg-config fuse --cflags --libs` -lulockmgr simplecowfs.c simplecow.c -o simplecowfs

holdopenfs: holdopenfs.c
	${CC} ${CFLAGS} -Wall `pkg-config fuse --cflags --libs` -lulockmgr holdopenfs.c -o holdopenfs
	
demotetoregularfilefs: demotetoregularfilefs.c
	gcc -Wall `pkg-config fuse --cflags --libs` -lulockmgr demotetoregularfilefs.c -o demotetoregularfilefs
