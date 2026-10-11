cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vdec_api)

# the JPEG, PNG and H.264 test pictures are the ones of the vdec samples
set(VDEC_SAMPLES $ENV{ZEPHYR_BASE}/samples/drivers/vdec)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
	${VDEC_SAMPLES}/jpeg/src
	${VDEC_SAMPLES}/png/src
	${VDEC_SAMPLES}/h264/src)
