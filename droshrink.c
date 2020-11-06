#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint32_t readUINT32LE(FILE *f)
{
	unsigned char c[4];
	fread(&c, 1, 4, f);
	return c[0] | (c[1] << 8L) | (c[2] << 16L) | (c[3] << 24L);
}

static int usage() {
	printf(
		"usage: droshrink N in.dro out.dro\n\n"
		"N: number of command pairs to be removed from end of file.\n"
	);
	return 1;
}

int main(int argc, char**argv) {
	if(argc != 4) return usage();

	unsigned n = atoi(argv[1]);

	FILE *in = fopen(argv[2], "r");
	FILE *out = fopen(argv[3], "w");

	if(!in || !out) {
		perror("fopen");
		return 1;
	}

	unsigned char cSig[9];
	cSig[8] = 0;
	fread(cSig, 1, 8, in);
	assert(!strcmp(cSig, "DBRAWOPL"));
	assert(readUINT32LE(in) == 2);

	fwrite(cSig, 1, 8, out);
	fwrite("\2\0\0\0", 1, 4, out);

	struct dro2hdr {
		uint32_t iLengthPairs;
		uint32_t iLengthMS;
		uint8_t iHardwareType;
		uint8_t iFormat;
		uint8_t iCompression;
		uint8_t iShortDelayCode;
		uint8_t iLongDelayCode;
		uint8_t iCodemapLength;
		uint8_t iCodemap[128];
	} dro2hdr = {0}, dro2hdr_out;
	dro2hdr.iLengthPairs = readUINT32LE(in);
	dro2hdr.iLengthMS = readUINT32LE(in);
	fread(&dro2hdr.iHardwareType, 1, 6, in);
	if(dro2hdr.iCodemapLength >= 128) {
		fprintf(stderr, "invalid setting %u for iCodemapLength!\n", (unsigned) dro2hdr.iCodemapLength);
		return 2;
	}
	fread(dro2hdr.iCodemap, 1, dro2hdr.iCodemapLength, in);
	dro2hdr_out = dro2hdr;
	dro2hdr_out.iLengthPairs -= n;
	/* FIXME: not big-endian safe */
	fwrite(&dro2hdr_out, 1, offsetof(struct dro2hdr, iCodemap), out);
	fwrite(dro2hdr_out.iCodemap, 1, dro2hdr.iCodemapLength, out);
	unsigned i;
	for(i=0; i<dro2hdr_out.iLengthPairs; ++i) {
		unsigned char buf[2];
		fread(buf, 1, 2, in);
		fwrite(buf, 1, 2, out);
	}
	unsigned long delay_ms = 0;
	for(; i<dro2hdr.iLengthPairs; ++i) {
		unsigned char buf[2];
		fread(buf, 1, 2, in);
		if(buf[0] == dro2hdr.iShortDelayCode)
			delay_ms += 1 + buf[1];
		else if(buf[0] == dro2hdr.iLongDelayCode)
			delay_ms += (1 + buf[1]) << 8;
	}
	dro2hdr_out.iLengthMS -= delay_ms;
	fseek(out, 16, SEEK_SET);
	fwrite(&dro2hdr_out.iLengthMS, 1, 4, out);
	fclose(out);
	fclose(in);
	printf("done\n");
	return 0;
}
