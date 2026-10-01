PREFIX ?= /usr/local
CFLAGS ?= -O2 -Wall -Wextra
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

vibematrix: vibematrix.c
	$(CC) $(CFLAGS) -DVERSION='"$(VERSION)"' -o $@ $< -framework OpenGL -framework CoreServices

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

# README video: record demo.sh activity headless with real timing, render offline.
# needs: brew install asciinema agg ffmpeg webp
REC := /tmp/vibematrix-rec
video: vibematrix
	rm -rf $(REC) && mkdir -p $(REC) assets
	# vibematrix runs in the foreground (it needs the terminal); a timer stops it after 32s
	asciinema rec --headless --window-size 128x36 --overwrite --command \
		'SPEED=1.5 ./demo.sh $(REC)/files 2>/dev/null & D=$$!; sleep 1; \
		 (sleep 32; pkill -TERM -x vibematrix -P $$$$) & ./vibematrix $(REC)/files; kill $$D; wait' \
		$(REC)/demo.cast
	agg --fps-cap 30 --font-size 10 --idle-time-limit 60 --last-frame-duration 0 $(REC)/demo.cast $(REC)/demo.gif
	ffmpeg -v error -y -i $(REC)/demo.gif -vf "scale=iw*2:ih*2:flags=neighbor,pad=ceil(iw/2)*2:ceil(ih/2)*2" \
		-c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart assets/demo.mp4
	ffmpeg -v error -y -i $(REC)/demo.gif \
		-vf "fps=15,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none" $(REC)/demo15.gif
	gif2webp -quiet -lossy -q 45 -m 4 $(REC)/demo15.gif -o assets/demo.webp
	rm -rf $(REC)

.PHONY: test install uninstall clean video
