ifndef PORT
PORT=/dev/ttyUSB0
endif
BOARD=esp8266:esp8266:nodemcuv2
XTAL=:xtal=160

# ESPTOOL=$(wildcard $(HOME)/.local/bin/esptool.py)
ESPTOOL=$(wildcard $(HOME)/.arduino15/packages/esp8266/hardware/esp8266/*/tools/esptool/esptool.py)

SRC = $(wildcard *.ino) $(wildcard *.h) html.h
PROJECT = $(notdir $(CURDIR))
TARGET = $(PROJECT).$(subst :,.,$(BOARD)).bin
FS_SRC= $(wildcard html/*)

$(TARGET): $(SRC)
	@rm -rf tmp
	@mkdir -p tmp
	TMPDIR=$(PWD)/tmp arduino-cli compile --fqbn=$(BOARD)$(XTAL)
	@rm -rf tmp

html.h: $(FS_SRC)
	./gen_html > $@

recompile: $(TARGET)

netupload: $(TARGET)
ifdef host
	curl -F "image=@$(TARGET)" ${host}:8266/update
else
	@echo Need host=target to be set - eg make $@ host=testesp
endif

upload:
	@mkdir -p tmp
	TMPDIR=$(PWD)/tmp arduino-cli upload --fqbn=$(BOARD) -p $(PORT)
	@rm -rf tmp

##	python $(ESPTOOL) --port=$(PORT) write_flash 0x0 $(TARGET)

serial:
	@kermit -l $(PORT) -b 115200 -c

clean:
	rm -rf *.elf tmp *.bin *.h
