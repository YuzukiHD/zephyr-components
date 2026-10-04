/* ***** BEGIN LICENSE BLOCK *****
 * Source last modified: $Id: main.c,v 1.4 2005/07/05 21:08:13 ehyche Exp $
 *
 * Portions Copyright (c) 1995-2005 RealNetworks, Inc. All Rights Reserved.
 *
 * The contents of this file, and the files included with this file,
 * are subject to the current version of the RealNetworks Public
 * Source License (the "RPSL") available at
 * http://www.helixcommunity.org/content/rpsl unless you have licensed
 * the file under the current version of the RealNetworks Community
 * Source License (the "RCSL") available at
 * http://www.helixcommunity.org/content/rcsl, in which case the RCSL
 * will apply. You may also obtain the license terms directly from
 * RealNetworks.  You may not use this file except in compliance with
 * the RPSL or, if you have a valid RCSL with RealNetworks applicable
 * to this file, the RCSL.  Please see the applicable RPSL or RCSL for
 * the rights, obligations and limitations governing use of the
 * contents of the file.
 *
 * This file is part of the Helix DNA Technology. RealNetworks is the
 * developer of the Original Code and owns the copyrights in the
 * portions it created.
 *
 * This file, and the files included with this file, is distributed
 * and made available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS
 * ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET
 * ENJOYMENT OR NON-INFRINGEMENT.
 *
 * Technology Compatibility Kit Test Suite(s) Location:
 *    http://www.helixcommunity.org/content/tck
 *
 * Contributor(s):
 *
 * ***** END LICENSE BLOCK ***** */

/**************************************************************************************
 * Fixed-point HE-AAC decoder
 * Jon Recker (jrecker@real.com)
 * February 2005
 *
 * main.c - sample command-line test wrapper for decoder
 **************************************************************************************/

#include <stdio.h>
#include <string.h>

#include "aacdec.h"
//#include "wrapper.h"

#define READBUF_SIZE	(2 * AAC_MAINBUF_SIZE * AAC_MAX_NCHANS)	/* pick something big enough to hold a bunch of frames */

#define SKIP_FRAMES		0		/* discard first SKIP_FRAMES decoded frames */

#ifdef AAC_ENABLE_SBR
#define SBR_MUL		2
#else
#define SBR_MUL		1
#endif

#if defined (__arm) && defined (__ARMCC_VERSION)
#define ARMULATE_MUL_FACT	1
#define MAX_FRAMES	-1
//#define MAX_FRAMES	300
#else
#define ARMULATE_MUL_FACT	1
#define MAX_FRAMES	-1
#endif

#define AACDEC_FOURCC_RIFF       0x52494646
#define AACDEC_FOURCC_WAVE       0x57415645
#define AACDEC_FOURCC_fmt        0x666d7420
#define AACDEC_FOURCC_data       0x64617461
#define AACDEC_FORMAT_PCM        0x0001

#if defined(WIN32) || defined(__ICCARM__)

void Usage(void)
{
	printf("\nUsage:\n");
	printf(">> aacdec [-wav] infile.aac outfile\n");
        printf("     -wav:  if specified, then outfile is a .wav file.\n");
        printf("            otherwise, outfile is a raw PCM file\n");
}

static int FillReadBuffer(unsigned char *readBuf, unsigned char *readPtr, int bufSize, int bytesLeft, FILE *infile)
{
	int nRead;

	/* move last, small chunk from end of buffer to start, then fill with new data */
	memmove(readBuf, readPtr, bytesLeft);
	nRead = fread(readBuf + bytesLeft, 1, bufSize - bytesLeft, infile);

	/* zero-pad to avoid finding false sync word after last frame (from old data in readBuf) */
	if (nRead < bufSize - bytesLeft)
		memset(readBuf + bytesLeft + nRead, 0, bufSize - bytesLeft - nRead);

	return nRead;
}


static short outBuf[AAC_MAX_NCHANS * AAC_MAX_NSAMPS * SBR_MUL];
static unsigned char readBuf[READBUF_SIZE];

int test_main(int argc, char **argv)
{
	int bytesLeft, nRead, err, outOfData, eofReached;
	unsigned char *readPtr;
	char *infileName, *outfileName;

	FILE *infile, *outfile/*, *logfile*/;
	HAACDecoder *hAACDecoder;
	AACFrameInfo aacFrameInfo;
	int nFrames;
        int TotalSamples=0;

#ifdef WIN32
	if (argc != 3)
	{
		Usage();
		return -1;
	}
        infileName = argv[1];
        outfileName = argv[2];


#elif defined(__ICCARM__)

        infileName = "../test/天边.aac";
        outfileName = "../test/天边_iar.pcm";

#endif

	/* open input file */
	infile = fopen(infileName, "rb");
	if (!infile) {
		printf(" *** Error opening input file %s ***\n", infileName);
		Usage();
		return -1;
	}

	/* open output file */
	if (strcmp(argv[2], "nul"))
	{
		outfile = fopen(outfileName, "wb");
		if (!outfile) {
			printf(" *** Error opening output file %s ***\n", outfileName);
			Usage();
			return -1;
		}
	}
	else
	{
		outfile = 0;	/* nul output */
	}

  //      int n=13;
    //   asm ("ssat %0, #16, %1" : "=r" ( n ) : "r"( n ) );


     //  int cpsr;
     //  asm volatile("mrs %0, PSR" : "=r"(cpsr));



	hAACDecoder = (HAACDecoder *)AACInitDecoder();

	if (!hAACDecoder)
	{
		printf(" *** Error initializing AAC decoder ***\n");
		Usage();
		return -1;
	}


	bytesLeft = 0;
	outOfData = 0;
	eofReached = 0;
	readPtr = readBuf;
	err = 0;


    nFrames=0;
	do {
		/* somewhat arbitrary trigger to refill buffer - should always be enough for a full frame */
		if (bytesLeft < AAC_MAX_NCHANS * AAC_MAINBUF_SIZE && !eofReached)
		{
			nRead = FillReadBuffer(readBuf, readPtr, READBUF_SIZE, bytesLeft, infile);
			bytesLeft += nRead;
			readPtr = readBuf;
			if (nRead == 0)
				eofReached = 1;
		}


		/* decode one AAC frame */
 		err = AACDecode(hAACDecoder, &readPtr, &bytesLeft, outBuf);



		if (err) {
			/* error occurred */
			switch (err) {
			case ERR_AAC_INDATA_UNDERFLOW:
				/* need to provide more data on next call to AACDecode() (if possible) */
				if (eofReached || bytesLeft == READBUF_SIZE)
					outOfData = 1;
				break;
			default:
				outOfData = 1;
				break;
			}
		}

		if (outOfData)
			break;

		/* no error */
		AACGetLastFrameInfo(hAACDecoder, &aacFrameInfo);

		fwrite(outBuf, aacFrameInfo.bitsPerSample / 8, aacFrameInfo.outputSamps, outfile);

                TotalSamples=TotalSamples+(aacFrameInfo.outputSamps/aacFrameInfo.nChans);

		nFrames++;
		printf("Frame:%d,TotalSamples:%d,\n",nFrames,TotalSamples);




	} while (1/*playStatus == Play*/);



	if (err != ERR_AAC_NONE && err != ERR_AAC_INDATA_UNDERFLOW)
		printf("\nError - %d", err);
	printf("\n");

	AACFreeDecoder(hAACDecoder);



	/* close files */
	fclose(infile);
	if (outfile)
        {
            fclose(outfile);
        }



	return 0;
}

#endif
