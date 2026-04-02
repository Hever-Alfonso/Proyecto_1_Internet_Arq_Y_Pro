CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS  = -pthread

SOURCES  = server.c          \
           equipment_state.c \
           connection_handler.c \
           metrics_processor.c \
           alert_system.c    \
           auth_service.c    \
           logger.c          \
           http_handler.c

OBJECTS  = $(SOURCES:.c=.o)
TARGET   = server

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

server.o:              server.c equipment_state.h logger.h alert_system.h \
                       metrics_processor.h connection_handler.h http_handler.h \
                       auth_service.h
equipment_state.o:     equipment_state.c equipment_state.h
connection_handler.o:  connection_handler.c connection_handler.h \
                       equipment_state.h alert_system.h auth_service.h logger.h
metrics_processor.o:   metrics_processor.c metrics_processor.h \
                       equipment_state.h alert_system.h logger.h
alert_system.o:        alert_system.c alert_system.h equipment_state.h
auth_service.o:        auth_service.c auth_service.h logger.h
logger.o:              logger.c logger.h
http_handler.o:        http_handler.c http_handler.h equipment_state.h \
                       alert_system.h connection_handler.h logger.h

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean