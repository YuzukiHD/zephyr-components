/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Host build of the same decoder: decodes an ADTS file and prints the CRC-32 of the PCM, to
 * compare with the board. usage: host_decode FILE.aac DESIRED_CHANNELS OUT.pcm
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pvmp4audiodecoder_api.h>

static uint8_t in[256 * 1024];
static int16_t pcm[2048 * 2];

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = data;

	crc = ~crc;
	while (len--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xedb88320U & -(crc & 1));
		}
	}
	return ~crc;
}

int main(int argc, char **argv)
{
	tPVMP4AudioDecoderExternal ext = {0};
	uint32_t crc = 0;
	int frames = 0;
	size_t n;
	void *mem;
	FILE *f;
	FILE *out;
	int desired;

	if (argc != 4) {
		fprintf(stderr, "usage: %s FILE.aac DESIRED_CHANNELS OUT.pcm\n", argv[0]);
		return 1;
	}
	f = fopen(argv[1], "rb");
	out = fopen(argv[3], "wb");
	if (f == NULL || out == NULL) {
		return 1;
	}
	n = fread(in, 1, sizeof(in), f);
	desired = atoi(argv[2]);
	mem = malloc(PVMP4AudioDecoderGetMemRequirements());

	ext.desiredChannels = desired;
	ext.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
	ext.aacPlusEnabled = false;
	PVMP4AudioDecoderInitLibrary(&ext, mem);
	ext.pInputBuffer = in;
	ext.inputBufferCurrentLength = n;
	while (ext.inputBufferUsedLength + 7 < (int)n) {
		int ret;

		ext.pOutputBuffer = pcm;
		ext.pOutputBuffer_plus = pcm + 2048;
		ret = PVMP4AudioDecodeFrame(&ext, mem);
		if (ret != MP4AUDEC_SUCCESS) {
			printf("error %d at frame %d\n", ret, frames);
			break;
		}
		crc = crc32_update(crc, pcm, ext.frameLength * desired * sizeof(int16_t));
		fwrite(pcm, sizeof(int16_t), ext.frameLength * desired, out);
		frames++;
	}
	printf("%d frames, %d Hz, CRC-32 %08x\n", frames, (int)ext.samplingRate, crc);
	fclose(out);
	free(mem);
	return 0;
}
