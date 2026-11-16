FQBN := esp32:esp32:esp32
PORT := /dev/ttyUSB1
BAUD := 115200
export TMPDIR := $(HOME)/tmp

.PHONY: compile upload monitor flash compdb clean

compile:
	arduino-cli compile -b $(FQBN) ./ --build-path ./build
	@$(MAKE) --no-print-directory map-ino

compdb:
	arduino-cli compile --only-compilation-database -b $(FQBN) ./ --build-path ./build
	@$(MAKE) --no-print-directory map-ino

map-ino:
	@python3 -c 'import json, os; p="build/compile_commands.json"; ino=os.path.abspath("victron-netsender.ino"); db=[e for e in json.load(open(p)) if e["file"]!=ino]; src=next((e for e in db if e["file"].endswith(".ino.cpp")), None); src and db.append(dict(src, file=ino)); json.dump(db, open(p,"w"), indent=2)'

upload: compile
	arduino-cli upload -p $(PORT) -b $(FQBN) --input-dir ./build

monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

flash: upload monitor

clean:
	rm -rf build
