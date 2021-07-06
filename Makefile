ifndef PORT
PORT=/dev/ttyUSB0
endif
BOARD=esp8266:esp8266:nodemcuv2
XTAL=:xtal=160

# If a new CA needs to be added, set it here and put the appropriate
# root public key in rootCA.h
CA=GODADDY
# CA=LETSENCRYPT

# ESPTOOL=$(wildcard $(HOME)/.local/bin/esptool.py)
ESPTOOL=$(wildcard $(HOME)/.arduino15/packages/esp8266/hardware/esp8266/*/tools/esptool/esptool.py)

SRC = $(wildcard *.ino) $(wildcard *.h) html.h
PROJECT = $(notdir $(CURDIR))
# TARGET = $(PROJECT).$(subst :,.,$(BOARD)).bin
# TARGET filename has changed in newer versions of arduino-cli
TARGET=$(PROJECT).ino.bin
FS_SRC= $(wildcard html/*)

$(TARGET): $(SRC) CA.h
	@rm -rf tmp
	@mkdir -p tmp
	TMPDIR=$(PWD)/tmp arduino-cli compile --fqbn=$(BOARD)$(XTAL) --output-dir $(PWD)
	@rm -rf tmp

html.h: $(FS_SRC)
	./gen_html > $@

# There has to be a better way of doing this, but this works!
CA.h: Makefile
	@echo "#define CA_$(CA) 1" > CA.h

recompile: $(TARGET)

netupload: $(TARGET)
ifdef host
	curl -F "image=@$(TARGET)" ${host}:8266/update
else
	@echo Need host=target to be set - eg make $@ host=testesp
endif

upload:
	@mkdir -p tmp
	TMPDIR=$(PWD)/tmp arduino-cli upload --fqbn=$(BOARD) -p $(PORT) --input-dir $(PWD)
	@rm -rf tmp

##	python $(ESPTOOL) --port=$(PORT) write_flash 0x0 $(TARGET)

serial:
	@kermit -l $(PORT) -b 115200 -c

clean:
	rm -rf *.elf tmp *.bin *.map CA.h html.h
