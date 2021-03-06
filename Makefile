CC = gcc
ECHO = echo
RM = rm -rf

CFLAGS = -Wall -funroll-loops -ggdb3 -DNDEBUG -std=c99 -pedantic -D_POSIX_C_SOURCE

BIN = shell352
OBJS = main.o


all: $(BIN) # etags

$(BIN): $(OBJS)
	@$(ECHO) Linking $@
	@$(CC) $^ -o $@

-include $(OBJS:.o=.d)

%.o: %.c
	@$(ECHO) Compiling $<
	@$(CC) $(CFLAGS) -MMD -MF $*.d -c $<

.PHONY: all clean clobber # etags

clean:
	@$(ECHO) Removing all generated files
	@$(RM) *.o $(BIN) *.d TAGS core vgcore.* gmon.out *.dSYM lshay

clobber: clean
	@$(ECHO) Removing backup files
	@$(RM) *~ \#* *pgm

package: clean $(BIN)
	@mkdir lshay
	@cp main.c Makefile lshay
	@zip -r lshay lshay 

# etags:
# 	@$(ECHO) Updating TAGS
# 	@etags *.[ch] *.cpp