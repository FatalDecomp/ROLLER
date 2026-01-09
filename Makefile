LIBS = 	-lc \
		-lm \
		-lz \
		-lSDL3 \
		-lSDL3_image \
		-lWildMidi

INCLUDES = 	-I. \
			-I./external/SDL3-3.2.14/include \
			-I./external/SDL3_image-3.2.4/include \
			-I./external/wildmidi-0.4.6/include

ifeq ($(BUILDCONFIG), debug)
	DEBUGFLAGS = -D _DEBUG -D DEBUG
else ifeq ($(BUILDCONFIG), release)
	DEBUGFLAGS = -D NDEBUG -O
endif

ARCH = x64
ARCHFLAGS = -m64
OUTPUTDIR = ./bin
TARGET = ./bin/ROLLER

FULLCONFIG=$(BUILDCONFIG)-$(ARCH)
CFLAGS = -g $(DEBUGFLAGS) $(INCLUDES) -Wno-format-truncation -Wno-format-overflow
LDFLAGS = -Wl,-R\$$ORIGIN -static-libgcc

.SUFFIXES: .o

SRC = 	PROJECTS/ROLLER/3d.c \
        PROJECTS/ROLLER/building.c \
        PROJECTS/ROLLER/car.c \
        PROJECTS/ROLLER/carplans.c \
        PROJECTS/ROLLER/cdx.c \
        PROJECTS/ROLLER/colision.c \
        PROJECTS/ROLLER/comms.c \
        PROJECTS/ROLLER/control.c \
        PROJECTS/ROLLER/date.c \
        PROJECTS/ROLLER/drawtrk3.c \
        PROJECTS/ROLLER/engines.c \
        PROJECTS/ROLLER/frontend.c \
        PROJECTS/ROLLER/func2.c \
        PROJECTS/ROLLER/func3.c \
        PROJECTS/ROLLER/function.c \
        PROJECTS/ROLLER/graphics.c \
        PROJECTS/ROLLER/horizon.c \
        PROJECTS/ROLLER/loadtrak.c \
        PROJECTS/ROLLER/mouse.c \
        PROJECTS/ROLLER/moving.c \
        PROJECTS/ROLLER/network.c \
        PROJECTS/ROLLER/plans.c \
        PROJECTS/ROLLER/polyf.c \
        PROJECTS/ROLLER/polytex.c \
        PROJECTS/ROLLER/replay.c \
        PROJECTS/ROLLER/roller.c \
        PROJECTS/ROLLER/rollercomms.c \
        PROJECTS/ROLLER/sound.c \
        PROJECTS/ROLLER/svgacpy.c \
        PROJECTS/ROLLER/tower.c \
        PROJECTS/ROLLER/transfrm.c \
        PROJECTS/ROLLER/userfns.c \
        PROJECTS/ROLLER/view.c

default: debug
debug: clean
	make -j 4 build BUILDCONFIG=debug
release: clean
	make -j 4 build BUILDCONFIG=release

build: ROLLER

obj/$(FULLCONFIG)/%.o: %.c
	$(CC) $(ARCHFLAGS) $(CFLAGS) -c -o $@ $<

OBJ = $(patsubst %.c,obj/$(FULLCONFIG)/%.o, $(SRC))
$(OBJ): | CREATE_OBJ_DIR

CREATE_OBJ_DIR:
	mkdir -p obj
	mkdir -p obj/$(FULLCONFIG)
	mkdir -p obj/$(FULLCONFIG)/PROJECTS
	mkdir -p obj/$(FULLCONFIG)/PROJECTS/ROLLER

ROLLER: $(OBJ) Makefile
	mkdir -p $(OUTPUTDIR)
	cp -R ./midi $(OUTPUTDIR)/midi
	$(CC) $(ARCHFLAGS) $(CFLAGS) $(LDFLAGS) -o $(OUTPUTDIR)/$@ $(OBJ) $(LIBS)

clean:
	rm -f ./bin/ROLLER
	rm -rf obj
