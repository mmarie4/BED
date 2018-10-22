NAME		= ez430-demo
NAMETAG		= ez430-demo-TAG
NAMEANCHOR	= ez430-demo-ANCHOR
LIBS		= -lez430
SRC		= main.c
SRC_DIR		= src
INC_DIR		= -I../../ez430-drivers/inc -Iprotothreads
OUT_DIR		= bin
LIB_DIR		= ../../ez430-drivers/lib
OBJ_DIR		= .obj
DOC_DIR		= doc
DEP_DIR 	= .deps
OBJ		= $(OBJ_DIR)/${NAMEANCHOR}.o $(OBJ_DIR)/${NAMETAG}.o
DEPS		= $(patsubst %.c,$(DEP_DIR)/%.d,$(SRC))
# Platform EZ430
CPU		= msp430f2274
CFLAGS		= -g -Wall -mmcu=${CPU} ${INC_DIR}
LDFLAGS		= -static -L${LIB_DIR} ${LIBS}
CC		= msp430-gcc
MAKEDEPEND	= ${CC} ${CFLAGS} -MM -MP -MT $@ -MF ${DEP_DIR}/main.d

all: ${OUT_DIR}/${NAMETAG}.elf ${OUT_DIR}/${NAMEANCHOR}.elf 

download-tag: all
	mspdebug rf2500 "prog ${OUT_DIR}/${NAMETAG}.elf"
download-anchor: all
	mspdebug rf2500 "prog ${OUT_DIR}/${NAMEANCHOR}.elf"

${OUT_DIR}/${NAMETAG}.elf: $(OBJ_DIR)/${NAMETAG}.o
	@mkdir -p ${OUT_DIR}
	${CC} -DTAG -mmcu=${CPU} $< ${LDFLAGS} -o $@
	msp430-size ${OUT_DIR}/${NAMETAG}.elf

${OUT_DIR}/${NAMEANCHOR}.elf: $(OBJ_DIR)/${NAMEANCHOR}.o
	@mkdir -p ${OUT_DIR}
	${CC} -DANCHOR -mmcu=${CPU} $< ${LDFLAGS} -o $@
	msp430-size ${OUT_DIR}/${NAMEANCHOR}.elf

$(OBJ_DIR)/${NAMETAG}.o: ${SRC_DIR}/main.c
	@mkdir -p ${OBJ_DIR} ${DEP_DIR}
	${MAKEDEPEND} $<
	${CC} ${CFLAGS} -DTAG -c $< -o $@

$(OBJ_DIR)/${NAMEANCHOR}.o: ${SRC_DIR}/main.c
	@mkdir -p ${OBJ_DIR} ${DEP_DIR}
	${MAKEDEPEND} $<
	${CC} ${CFLAGS} -DANCHOR -c $< -o $@

-include ${DEPS}

.PHONY: clean
clean:
	@rm -Rf ${OUT_DIR} ${OBJ_DIR} ${DEP_DIR} ${DOC_DIR}

.PHONY: rebuild
rebuild: clean all

.PHONY: doc
doc:
	doxygen

