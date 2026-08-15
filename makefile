#project name
target := Launcher

#directories
buildDir := ./build
sourceDir := ./src

#compiler stuff
CC := clang
CFLAGS := -Wall -g $(shell pkg-config --cflags librsvg-2.0)
libFlags := -lSDL3 -lSDL3_ttf -lpng $(shell pkg-config --libs librsvg-2.0)

#make file instructions ahead, generaly don't touch

#file finding stuffs
cFiles := $(wildcard $(sourceDir)/*/*.c) $(wildcard $(sourceDir)/*.c) $(wildcard $(sourceDir)/*/*/*.c)
OBJ := $(patsubst $(sourceDir)/%.c, $(buildDir)/%.o, $(cFiles))

#make obj files in build from c files in src
$(buildDir)/%.o: ./$(sourceDir)/%.c ./$(sourceDir)/%.h
	mkdir -p $(@D)
	$(CC) -c -o $@ $< $(CFLAGS)

#build the main program out of the obj files in build
$(target): ${OBJ}
	$(CC) -o $(target) $(OBJ) $(CFLAGS) $(libFlags)

#usefull stuffs
.PHONY: all run clean test

test:
	echo $(OBJ)

all: $(target)

run: all
	./$(target)

clean:
	rm -rf $(buildDir)
	rm $(target)
