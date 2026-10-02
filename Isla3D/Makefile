BUILD = build
GAME_IMG = ../Confidential/assets/img
CHROMIUM = chromium --headless=new --no-sandbox --enable-unsafe-swiftshader --use-angle=swiftshader \
	--window-size=1920,1080 --virtual-time-budget=60000
PANO_URL = file://$(CURDIR)/$(BUILD)/isla_3d.html?warm=2000&clean
PANO1_CAM = -120,105,-760,0,12,0
PANO2_CAM = 700,130,520,0,8,-40

.PHONY: all data model viewer open clean pano orbit

all: data model viewer

data:
	python3 scripts/prepare_island.py $(BUILD)

model:
	blender -b -P scripts/build_island_blender.py -- $(BUILD)

viewer:
	python3 scripts/make_viewer.py $(BUILD)/little_st_james.glb $(BUILD)/isla_3d.html

open:
	xdg-open $(BUILD)/isla_3d.html

pano: viewer
	mkdir -p $(BUILD)/pano
	$(CHROMIUM) --user-data-dir=/tmp/isla_pano1 --screenshot=$(CURDIR)/$(BUILD)/pano/pano1_full.png "$(PANO_URL)#cam=$(PANO1_CAM)" 2>/dev/null & \
	$(CHROMIUM) --user-data-dir=/tmp/isla_pano2 --screenshot=$(CURDIR)/$(BUILD)/pano/pano2_full.png "$(PANO_URL)#cam=$(PANO2_CAM)" 2>/dev/null & \
	wait
	python3 -c "from PIL import Image; import sys; \
	[Image.open(f'$(BUILD)/pano/pano{i}_full.png').convert('RGB').resize((480, 270), Image.NEAREST).save(f'$(GAME_IMG)/isla_pano{i}.png') for i in (1, 2)]"

orbit: viewer
	python3 scripts/record_orbit.py $(BUILD) $(GAME_IMG)/orbit 360 15

clean:
	rm -f $(BUILD)/*.blend1
