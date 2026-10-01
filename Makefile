PREFIX ?= /usr/local
CFLAGS ?= -O2 -Wall -Wextra

vibematrix: vibematrix.c
	$(CC) $(CFLAGS) -o $@ $< -framework OpenGL -framework CoreServices

test: vibematrix
	./vibematrix --test

install: vibematrix
	install -d $(PREFIX)/bin $(PREFIX)/share/vibematrix/shader
	install -m 755 vibematrix $(PREFIX)/bin/
	install -m 644 shader/*.glsl $(PREFIX)/share/vibematrix/shader/

uninstall:
	rm -f $(PREFIX)/bin/vibematrix
	rm -rf $(PREFIX)/share/vibematrix

clean:
	rm -f vibematrix

.PHONY: test install uninstall clean
