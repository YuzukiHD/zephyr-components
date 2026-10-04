/* ***** BEGIN LICENSE BLOCK *****
 * Source last modified: $Id: filefmt.c,v 1.1 2005/02/26 01:47:34 jrecker Exp $
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
 * filefmt.c - ADIF and ADTS header decoding, raw block handling
 **************************************************************************************/
#include <stdio.h>
#include "coder.h"
#include "stddef.h"

//#define AAC_LATM_DEBUG
 /**************************************************************************************
 * Function:    UnpackADTSHeader
 *
 * Description: parse the ADTS frame header and initialize decoder state
 *
 * Inputs:      valid AACDecInfo struct
 *              double pointer to buffer with complete ADTS frame header (byte aligned)
 *                header size = 7 bytes, plus 2 if CRC
 *
 * Outputs:     filled in ADTS struct
 *              updated buffer pointer
 *              updated bit offset
 *              updated number of available bits
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * TODO:        test CRC
 *              verify that fixed fields don't change between frames
 **************************************************************************************/
int UnpackADTSHeader(AACDecInfo *aacDecInfo, unsigned char **buf, int *bitOffset, int *bitsAvail)
{
	int bitsUsed;
	PSInfoBase *psi;
	BitStreamInfo bsi;
	ADTSHeader *fhADTS;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoBase *)(aacDecInfo->psInfoBase);
	fhADTS = &(psi->fhADTS);

	/* init bitstream reader */
	SetBitstreamPointer(&bsi, (*bitsAvail + 7) >> 3, *buf);
	// GetBits(&bsi, *bitOffset);

	/* verify that first 12 bits of header are syncword */
	if (GetBits(&bsi, 12) != 0x0fff)
		return ERR_AAC_INVALID_ADTS_HEADER;

	/* fixed fields - should not change from frame to frame */
	fhADTS->id =               GetBits(&bsi, 1);
	fhADTS->layer =            GetBits(&bsi, 2);
	fhADTS->protectBit =       GetBits(&bsi, 1);
	fhADTS->profile =          GetBits(&bsi, 2);
	fhADTS->sampRateIdx =      GetBits(&bsi, 4);
	fhADTS->privateBit =       GetBits(&bsi, 1);
	fhADTS->channelConfig =    GetBits(&bsi, 3);
	fhADTS->origCopy =         GetBits(&bsi, 1);
	fhADTS->home =             GetBits(&bsi, 1);

	/* variable fields - can change from frame to frame */
	fhADTS->copyBit =          GetBits(&bsi, 1);
	fhADTS->copyStart =        GetBits(&bsi, 1);
	fhADTS->frameLength =      GetBits(&bsi, 13);
	fhADTS->bufferFull =       GetBits(&bsi, 11);
	fhADTS->numRawDataBlocks = GetBits(&bsi, 2) + 1;



	/* note - MPEG4 spec, correction 1 changes how CRC is handled when protectBit == 0 and numRawDataBlocks > 1 */
	if (fhADTS->protectBit == 0)
		fhADTS->crcCheckWord = GetBits(&bsi, 16);

	/* byte align */
	ByteAlignBitstream(&bsi);	/* should always be aligned anyway */

	/* check validity of header */
	if (fhADTS->layer != 0 || fhADTS->profile != AAC_PROFILE_LC ||
		fhADTS->sampRateIdx >= NUM_SAMPLE_RATES || fhADTS->channelConfig >= NUM_DEF_CHAN_MAPS)
		return ERR_AAC_INVALID_ADTS_HEADER;

#ifndef AAC_ENABLE_MPEG4
	if (fhADTS->id != 1)
		return ERR_AAC_MPEG4_UNSUPPORTED;
#endif

	/* update codec info */
	psi->sampRateIdx = fhADTS->sampRateIdx;
	if (!psi->useImpChanMap)
		psi->nChans = channelMapTab[fhADTS->channelConfig];

	/* syntactic element fields will be read from bitstream for each element */
	aacDecInfo->prevBlockID = AAC_ID_INVALID;
	aacDecInfo->currBlockID = AAC_ID_INVALID;
	aacDecInfo->currInstTag = -1;

	/* fill in user-accessible data (TODO - calc bitrate, handle tricky channel config cases) */
	aacDecInfo->bitRate = 0;
	aacDecInfo->nChans = psi->nChans;
	aacDecInfo->sampRate = sampRateTab[psi->sampRateIdx];
	aacDecInfo->profile = fhADTS->profile;
	aacDecInfo->sbrEnabled = 0;
	aacDecInfo->adtsBlocksLeft = fhADTS->numRawDataBlocks;

	/* update bitstream reader */
	bitsUsed = CalcBitsUsed(&bsi, *buf, *bitOffset);
	*buf += (bitsUsed + *bitOffset) >> 3;
	*bitOffset = (bitsUsed + *bitOffset) & 0x07;
	*bitsAvail -= bitsUsed ;
	if (*bitsAvail < 0)
		return ERR_AAC_INDATA_UNDERFLOW;

	return ERR_AAC_NONE;
}

/* XXX: make sure to update the copies in the different encoders if you change
* this table */
const int avpriv_mpeg4audio_sample_rates[16] = {
	96000, 88200, 64000, 48000, 44100, 32000,
	24000, 22050, 16000, 12000, 11025, 8000, 7350
};

const unsigned char ff_mpeg4audio_channels[8] = {
	0, 1, 2, 3, 4, 5, 6, 8
};

#define MKBETAG(a,b,c,d) ((d) | ((c) << 8) | ((b) << 16) | ((unsigned)(a) << 24))




unsigned int LatmGetValue(BitStreamInfo* bsi)
{
    unsigned char  bytesForValue, valueTmp = 0;
    unsigned int value = 0; /* helper variable 32bit */

    bytesForValue = GetBits(bsi, 2);
    for ( unsigned int i = 0; i <= bytesForValue; i++ ) {
        value <<= 8;
        valueTmp = GetBits(bsi, 8);
        value += valueTmp;
    }
    return value;
}

enum AudioObjectType GetAudioObjectType(BitStreamInfo* bsi)
{
    int audioObjectType = 0;
    audioObjectType = GetBits(bsi, 5);

    if(audioObjectType == AOT_ESCAPE){
        int audioObjectTypeExt = GetBits(bsi, 6);
        audioObjectType = 32 + audioObjectTypeExt;
    }
    return (enum AudioObjectType)audioObjectType;
}

int GASpecificConfig(LATMHeader * fhLATM, BitStreamInfo* bsi)
{
    fhLATM->frameLengthFlag = GetBits(bsi, 1);/* frameLengthFlag: 1 for a 960/480 (I)MDCT, 0 for a 1024/512 (I)MDCT*/

    fhLATM->dependsOnCoreCoder = GetBits(bsi, 1); /* dependsOnCoreCoder: Sampling Rate Coder Specific, see in ISO/IEC 14496-3 Subpart 4, 4.4.1 */
    if (fhLATM->dependsOnCoreCoder){
        fhLATM->coreCoderDelay = GetBits(bsi, 14); /* Extension Flag: Shall be 1 for aot = 17,19,20,21,22,23 */
    }

    fhLATM->extension_flag = GetBits(bsi, 1);
#ifdef AAC_LATM_DEBUG
    printf("dependsOnCoreCoder %d \n", fhLATM->dependsOnCoreCoder);
#endif
    if(fhLATM->chan_config == 0){
        printf("loas head err %d \n", __LINE__);
        return ERR_AAC_INVALID_LATM_HEADER;
    }
#ifdef AAC_LATM_DEBUG
    printf("fhLATM->extension_flag = %d %d\n", fhLATM->extension_flag, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
    if (fhLATM->object_type == AOT_AAC_SCAL ||
                             fhLATM->object_type == AOT_ER_AAC_SCAL){
        GetBits(bsi, 3);     // layerNr
    }

    if(fhLATM->extension_flag){
        if(fhLATM->object_type == AOT_ER_BSAC){
             GetBits(bsi, 5);     // numberOfSubFrame
             GetBits(bsi, 11);   //layer_length
        }

        if ((fhLATM->object_type == AOT_ER_AAC_LC)   ||
            (fhLATM->object_type == AOT_ER_AAC_LTP)  ||
            (fhLATM->object_type == AOT_ER_AAC_SCAL) ||
            (fhLATM->object_type == AOT_ER_AAC_LD)){
            GetBits(bsi, 1); //aacSectionDataResilienceFlag
            GetBits(bsi, 1); //aacScalefactorDataResilienceFlag
            GetBits(bsi, 1); //aacSpectralDataResilienceFlag
        }

         GetBits(bsi, 1); //extensionFlag3
    }
#ifdef AAC_LATM_DEBUG
    printf("leave %s %d\n", __func__, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
    return 0;
}

int AudioSpecificConfig (LATMHeader * fhLATM, BitStreamInfo* bsi)
{
    int result = 0;
    int BitUsed = 0, BitUsedOld = 0;
#ifdef AAC_LATM_DEBUG
    printf("enter %s \n", __func__);
#endif
    BitUsedOld = CalcBitsUsed(bsi, fhLATM->buf_start, 0);
    fhLATM->object_type = GetAudioObjectType(bsi);

    fhLATM->sampling_index = GetBits(bsi, 4);

    if (fhLATM->sampling_index==0x0f){
    	fhLATM->sample_rate = GetBits(bsi, 24);
    }
    else{
    	fhLATM->sample_rate = avpriv_mpeg4audio_sample_rates[fhLATM->sampling_index];
    }

    fhLATM->chan_config = GetBits(bsi, 4);
    if (fhLATM->chan_config < 8)
        fhLATM->channels = ff_mpeg4audio_channels[fhLATM->chan_config];

    fhLATM->sbr = -1;
    fhLATM->ps = -1;
#ifdef AAC_LATM_DEBUG
    printf("fhLATM->object_type %d %d\n", fhLATM->object_type, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
    if((fhLATM->object_type == AOT_SBR) || (fhLATM->object_type == AOT_PS)){
        fhLATM->ext_object_type = AOT_SBR;
        fhLATM->sbr = 1;
        if (fhLATM->object_type == AOT_PS)
            fhLATM->ps = 1;
        fhLATM->ext_sampling_index = GetBits(bsi, 4);
        if (fhLATM->ext_sampling_index == 0x0f){
            fhLATM->ext_sample_rate = GetBits(bsi, 24);
        }
        else{
            fhLATM->ext_sample_rate = avpriv_mpeg4audio_sample_rates[fhLATM->ext_sampling_index];
        }

        fhLATM->ext_object_type = GetAudioObjectType(bsi);
    }else{
        fhLATM->ext_object_type = AOT_NULL_OBJECT;
        fhLATM->ext_sample_rate = 0;
    }

    switch(fhLATM->object_type){
        case AOT_AAC_LC:
        case AOT_ER_AAC_LC:
        case AOT_ER_AAC_LD:
        case AOT_ER_AAC_SCAL:
        case AOT_ER_BSAC:
            result = GASpecificConfig(fhLATM, bsi);
        break;

        default:
            result = -1;
            printf("AudioSpecificConfig object_type not support:%d\n", fhLATM->object_type);
        break;
    }

    if(!result){
        BitUsed = CalcBitsUsed(bsi, fhLATM->buf_start, 0);
        result = BitUsed - BitUsedOld;
    }
#ifdef AAC_LATM_DEBUG
    printf("leave %s %d\n", __func__, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
    return result;
}

int StreamMuxConfig(LATMHeader * fhLATM, BitStreamInfo* bsi)
{
    int result = ERR_AAC_NONE;
    LATM_LAYER_INFO *p_linfo = NULL;
#ifdef AAC_LATM_DEBUG
    printf("enter %s \n", __func__);
#endif
    fhLATM->audio_mux_version = GetBits(bsi, 1);
#ifdef AAC_LATM_DEBUG
    printf("fhLATM->audio_mux_version = %d\n", fhLATM->audio_mux_version);
#endif
    if (fhLATM->audio_mux_version){
    	fhLATM->audio_mux_version_A = GetBits(bsi, 1);
    }else{
        fhLATM->audio_mux_version_A = 0;
    }
#ifdef AAC_LATM_DEBUG
    printf("fhLATM->audio_mux_version_A = %d\n", fhLATM->audio_mux_version_A);
#endif
    if(fhLATM->audio_mux_version_A == 0){
        int streamCnt = 0;
        if(fhLATM->audio_mux_version == 1){
            fhLATM->taraFullness = LatmGetValue(bsi);
        }

        fhLATM->allStreamSameTimeFraming = GetBits(bsi, 1);           // allStreamSameTimeFraming
        fhLATM->numSubFrames			 = GetBits(bsi, 6);       // numSubFrames
        fhLATM->numPrograms = GetBits(bsi, 4);                                 // numPrograms

	if (fhLATM->numPrograms){
		printf("loas head err %d \n", __LINE__);
		return ERR_AAC_INVALID_LATM_HEADER;
	}

        for(int prog = 0; prog <= fhLATM->numPrograms; prog ++){
            fhLATM->numLayer = GetBits(bsi, 3);           // numLayer
            if (fhLATM->numLayer > 1){
                printf("loas head err %d \n", __LINE__);
                return ERR_AAC_INVALID_LATM_HEADER;
            }

             for (int lay = 0; lay <= fhLATM->numLayer; lay++) {
                p_linfo = &fhLATM->m_linfo[prog][lay];
                p_linfo->streamID = streamCnt++;
                p_linfo->frameLengthInBits = 0;


                if( (prog == 0) && (lay == 0) ) {
                    fhLATM->useSameConfig = 0;
                } else {
                    fhLATM->useSameConfig = GetBits(bsi,1);
                }

                if(fhLATM->useSameConfig){
                    printf("loas head err %d \n", __LINE__);
                    return ERR_AAC_INVALID_LATM_HEADER;
                }

                if(fhLATM->audio_mux_version == 0){
                    AudioSpecificConfig(fhLATM, bsi);
                }else{
                    unsigned int ascLen = 0;
                    ascLen = LatmGetValue(bsi);
#ifdef AAC_LATM_DEBUG
                    printf("ascLen = %d %d\n",ascLen, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
                    result = AudioSpecificConfig(fhLATM, bsi);
                    if(result >=0){
                        ascLen -= result;
                        fhLATM->specific_config_bitindex = ascLen;
#ifdef AAC_LATM_DEBUG
                        printf("ascLen = %d result = %d \n", ascLen, result);
#endif
                        GetBits(bsi, ascLen);
                    }else
                        return result;
                }

                p_linfo->frameLengthType = GetBits(bsi, 3);
#ifdef AAC_LATM_DEBUG
                printf("p_linfo->frameLengthType = %d %d \n", p_linfo->frameLengthType, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
                switch( p_linfo->frameLengthType ) {
                    case 0:
                        p_linfo->latmBufferFullness = GetBits(bsi, 8);
                        if(!fhLATM->allStreamSameTimeFraming){
                              printf("loas head err %d \n", __LINE__);
                              return ERR_AAC_INVALID_LATM_HEADER;
                        }
                    break;

                    case 1:
                        p_linfo->frameLength = GetBits(bsi, 9);
                    case 3:
                    case 4:
                    case 5:
                        GetBits(bsi, 6);       // CELP frame length table index
                    break;
                    case 6:
                    case 7:
			GetBits(bsi, 1);       // HVXC frame length table index
			break;
                    /* HVXC */
                    default:
                        printf("loas head err %d \n", __LINE__);
                        return ERR_AAC_INVALID_LATM_HEADER; //_LATM_INVALIDFRAMELENGTHTYPE;
                }  /* switch framelengthtype*/
             }
        }

        fhLATM->otherDataPresent = GetBits(bsi, 1);
#ifdef AAC_LATM_DEBUG
        printf("fhLATM->otherDataPresent = %d latmBufferFullness:%d\n", fhLATM->otherDataPresent, p_linfo->latmBufferFullness);
#endif
        if(fhLATM->otherDataPresent){
            if(fhLATM->audio_mux_version == 1){
                fhLATM->otherDataLenBits = LatmGetValue(bsi); //just read otherDataLenBits
            }
            else{
                int otherDataLenBits = 0 , otherDataLenEsc = 0;
                do{
                    otherDataLenBits <<= 8;
                    otherDataLenEsc = GetBits(bsi, 1);
                    otherDataLenBits += GetBits(bsi, 8);
                }while(otherDataLenEsc);
                fhLATM->otherDataLenBits = otherDataLenBits;
            }
        }

        {
            unsigned char crcCheckPresent;
            crcCheckPresent = GetBits(bsi, 1);
#ifdef AAC_LATM_DEBUG
            printf("crcCheckPresent %d %d\n",crcCheckPresent, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
            if(crcCheckPresent)
                GetBits(bsi, 8); //crcCheckSum
        }
#ifdef AAC_LATM_DEBUG
        printf("leave %s %d\n", __func__, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
    }else{
        printf("loas head err %d \n", __LINE__);
        return ERR_AAC_INVALID_LATM_HEADER;
    }

    result = result;
    return ERR_AAC_NONE;
}

int PayloadLengthInfo(LATMHeader * fhLATM, BitStreamInfo* bsi)
{

    int result = ERR_AAC_NONE;
    unsigned char endFlag = 0;

    if(fhLATM->allStreamSameTimeFraming){
        for(int prog = 0; prog <= fhLATM->numPrograms;  prog ++){
            for(int lay = 0; lay <= fhLATM->numLayer; lay ++){
                unsigned char tmp;
                LATM_LAYER_INFO *p_linfo = &fhLATM->m_linfo[prog][lay];
#ifdef AAC_LATM_DEBUG
                printf("pl info %d\n", p_linfo->frameLengthType);
#endif
                if(p_linfo->frameLengthType == 0){
                    p_linfo->MuxSlotLengthBytes = 0;
                    do{
                        tmp = GetBits(bsi, 8);
                        if(tmp != 0xff)
                            endFlag = 1;
#ifdef AAC_LATM_DEBUG
                        printf("MuxSlotLengthBytes = %d %d %d\n", p_linfo->MuxSlotLengthBytes, tmp, CalcBitsUsed(bsi, fhLATM->buf_start, 0));
#endif
                        p_linfo->MuxSlotLengthBytes += tmp;
                    }while(endFlag == 0);
                }else{
                    if(p_linfo->frameLengthType == 3 ||
                        p_linfo->frameLengthType == 5 ||
                        p_linfo->frameLengthType == 7){
                        p_linfo->MuxSlotLengthCoded = GetBits(bsi, 2);
                    }
                }
            }
        }
    }else{
        printf("loas head err %d \n", __LINE__);
        return ERR_AAC_INVALID_LATM_HEADER;
    }

    return result;
}

/**************************************************************************************
* Function:    UnpackLATMHeader
*
* Description: parse the LATM frame header and initialize decoder state
*
* Inputs:      valid AACDecInfo struct
*              double pointer to buffer with complete LATM frame header (byte aligned)
*                header size = 7 bytes, plus 2 if CRC
*
* Outputs:     filled in LATM struct
*              updated buffer pointer
*              updated bit offset
*              updated number of available bits
*
* Return:      0 if successful, error code (< 0) if error
*
* TODO:        test CRC
*              verify that fixed fields don't change between frames
**************************************************************************************/
int UnpackLATMHeader(AACDecInfo *aacDecInfo, unsigned char **buf, int *bitOffset, int *bitsAvail)
{
    int result = ERR_AAC_NONE;
    int bitsUsed = 0;
    LATMHeader *fhLATM;
    PSInfoBase *psi;
    BitStreamInfo bsi;
    /* validate pointers */
    if (!aacDecInfo || !aacDecInfo->psInfoBase)
    	return ERR_AAC_NULL_POINTER;
    psi = (PSInfoBase *)(aacDecInfo->psInfoBase);
    fhLATM = &(psi->fhLATM);

    fhLATM->buf_start = *buf;
    /* init bitstream reader */
    SetBitstreamPointer(&bsi, (*bitsAvail + 7) >> 3, *buf);

    fhLATM->use_same_mux = GetBits(&bsi, 1);
    if (!fhLATM->use_same_mux){
        result = StreamMuxConfig(fhLATM, &bsi);
#ifdef AAC_LATM_DEBUG
        printf("StreamMuxConfig %d \n" ,CalcBitsUsed(&bsi, fhLATM->buf_start, 0));
#endif
    }
#ifdef AAC_LATM_DEBUG
    printf("loas head:%d %d %d %d %d %d %d %d\n", fhLATM->audio_mux_version, fhLATM->audio_mux_version_A,
                fhLATM->chan_config, fhLATM->sampling_index, fhLATM->allStreamSameTimeFraming,
                fhLATM->object_type, fhLATM->numSubFrames,  fhLATM->use_same_mux);
#endif

    if(fhLATM->audio_mux_version_A == 0){
        for(int i = 0; i <= fhLATM->numSubFrames; i ++){
            PayloadLengthInfo(fhLATM, &bsi);
        }

        if(fhLATM->otherDataPresent){
            for(int i = 0; i < fhLATM->otherDataLenBits; i ++)
                GetBits(&bsi, 1);
        }
    }else{
        /* audioMuxVersionA > 0 is reserved for future extensions */
        result = ERR_AAC_INVALID_LATM_HEADER;
        printf("loas head err %d \n", __LINE__);
    }
#ifdef AAC_LATM_DEBUG
    printf("loas head end %d \n", CalcBitsUsed(&bsi, fhLATM->buf_start, 0));
#endif
    ByteAlignBitstream(&bsi);	/* should always be aligned anyway */

/* update codec info */
    psi->sampRateIdx = fhLATM->sampling_index;
    if (!psi->useImpChanMap)
    	psi->nChans = channelMapTab[fhLATM->chan_config];

    /* syntactic element fields will be read from bitstream for each element */
    aacDecInfo->prevBlockID = AAC_ID_INVALID;
    aacDecInfo->currBlockID = AAC_ID_INVALID;
    aacDecInfo->currInstTag = -1;

    /* fill in user-accessible data (TODO - calc bitrate, handle tricky channel config cases) */
    aacDecInfo->bitRate = 0;
    aacDecInfo->nChans = psi->nChans;
    aacDecInfo->sampRate = sampRateTab[psi->sampRateIdx];

    aacDecInfo->profile = 0;

    if (fhLATM->object_type == AOT_AAC_LC)
    {
    	aacDecInfo->profile = AAC_PROFILE_LC;
    }
    else if (fhLATM->object_type == AOT_AAC_SSR)
    {
    	aacDecInfo->profile = AAC_PROFILE_SSR;
    }
    aacDecInfo->sbrEnabled = (fhLATM->sbr == 1)?1:0;

    bitsUsed = CalcBitsUsed(&bsi, *buf, *bitOffset);
    *buf += (bitsUsed + *bitOffset) >> 3;
    *bitOffset = (bitsUsed + *bitOffset) & 0x07;
    *bitsAvail -= bitsUsed;
#ifdef AAC_LATM_DEBUG
    printf("loas bitOffset=%d bitsUsed = %d\n", *bitOffset, bitsUsed);
    printf("buf = 0x%x 0x%x 0x%x 0x%x \n", (*buf)[0],(*buf)[1],(*buf)[2],(*buf)[3]);
#endif
    if ((*bitsAvail < 0) || (*bitsAvail < (fhLATM->m_linfo[0][0].MuxSlotLengthBytes*8)))
    	return ERR_AAC_INDATA_UNDERFLOW;

    result = result;
    return ERR_AAC_NONE;
}


/**************************************************************************************
 * Function:    GetADTSChannelMapping
 *
 * Description: determine the number of channels from implicit mapping rules
 *
 * Inputs:      valid AACDecInfo struct
 *              pointer to start of raw_data_block
 *              bit offset
 *              bits available
 *
 * Outputs:     updated number of channels
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Notes:       calculates total number of channels using rules in 14496-3, 4.5.1.2.1
 *              does not attempt to deduce speaker geometry
 **************************************************************************************/
int GetADTSChannelMapping(AACDecInfo *aacDecInfo, unsigned char *buf, int bitOffset, int bitsAvail)
{
	int ch, nChans, elementChans, err;
	PSInfoBase *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoBase *)(aacDecInfo->psInfoBase);

	nChans = 0;
	do {
		/* parse next syntactic element */
		err = DecodeNextElement(aacDecInfo, &buf, &bitOffset, &bitsAvail);
		if (err)
			return err;

		elementChans = elementNumChans[aacDecInfo->currBlockID];
		nChans += elementChans;

		for (ch = 0; ch < elementChans; ch++) {
			err = DecodeNoiselessData(aacDecInfo, &buf, &bitOffset, &bitsAvail, ch);
			if (err)
				return err;
		}
	} while (aacDecInfo->currBlockID != AAC_ID_END);

	if (nChans <= 0)
		return ERR_AAC_CHANNEL_MAP;

	/* update number of channels in codec state and user-accessible info structs */
	psi->nChans = nChans;
	aacDecInfo->nChans = psi->nChans;
	psi->useImpChanMap = 1;

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    GetNumChannelsADIF
 *
 * Description: get number of channels from program config elements in an ADIF file
 *
 * Inputs:      array of filled-in program config element structures
 *              number of PCE's
 *
 * Outputs:     none
 *
 * Return:      total number of channels in file
 *              -1 if error (invalid number of PCE's or unsupported mode)
 **************************************************************************************/
static int GetNumChannelsADIF(ProgConfigElement *fhPCE, int nPCE)
{
	int i, j, nChans;

	if (nPCE < 1 || nPCE > MAX_NUM_PCE_ADIF)
		return -1;

	nChans = 0;
	for (i = 0; i < nPCE; i++) {
		/* for now: only support LC, no channel coupling */
		if (fhPCE[i].profile != AAC_PROFILE_LC || fhPCE[i].numCCE > 0)
			return -1;

		/* add up number of channels in all channel elements (assume all single-channel) */
        nChans += fhPCE[i].numFCE;
        nChans += fhPCE[i].numSCE;
        nChans += fhPCE[i].numBCE;
        nChans += fhPCE[i].numLCE;

		/* add one more for every element which is a channel pair */
        for (j = 0; j < fhPCE[i].numFCE; j++) {
            if (CHAN_ELEM_IS_CPE(fhPCE[i].fce[j]))
                nChans++;
        }
        for (j = 0; j < fhPCE[i].numSCE; j++) {
            if (CHAN_ELEM_IS_CPE(fhPCE[i].sce[j]))
                nChans++;
        }
        for (j = 0; j < fhPCE[i].numBCE; j++) {
            if (CHAN_ELEM_IS_CPE(fhPCE[i].bce[j]))
                nChans++;
        }

	}

	return nChans;
}

/**************************************************************************************
 * Function:    GetSampleRateIdxADIF
 *
 * Description: get sampling rate index from program config elements in an ADIF file
 *
 * Inputs:      array of filled-in program config element structures
 *              number of PCE's
 *
 * Outputs:     none
 *
 * Return:      sample rate of file
 *              -1 if error (invalid number of PCE's or sample rate mismatch)
 **************************************************************************************/
static int GetSampleRateIdxADIF(ProgConfigElement *fhPCE, int nPCE)
{
	int i, idx;

	if (nPCE < 1 || nPCE > MAX_NUM_PCE_ADIF)
		return -1;

	/* make sure all PCE's have the same sample rate */
	idx = fhPCE[0].sampRateIdx;
	for (i = 1; i < nPCE; i++) {
		if (fhPCE[i].sampRateIdx != idx)
			return -1;
	}

	return idx;
}

/**************************************************************************************
 * Function:    UnpackADIFHeader
 *
 * Description: parse the ADIF file header and initialize decoder state
 *
 * Inputs:      valid AACDecInfo struct
 *              double pointer to buffer with complete ADIF header
 *                (starting at 'A' in 'ADIF' tag)
 *              pointer to bit offset
 *              pointer to number of valid bits remaining in inbuf
 *
 * Outputs:     filled-in ADIF struct
 *              updated buffer pointer
 *              updated bit offset
 *              updated number of available bits
 *
 * Return:      0 if successful, error code (< 0) if error
 **************************************************************************************/
int UnpackADIFHeader(AACDecInfo *aacDecInfo, unsigned char **buf, int *bitOffset, int *bitsAvail)
{
	int i, bitsUsed;
	PSInfoBase *psi;
	BitStreamInfo bsi;
	ADIFHeader *fhADIF;
	ProgConfigElement *pce;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoBase *)(aacDecInfo->psInfoBase);

	/* init bitstream reader */
	SetBitstreamPointer(&bsi, (*bitsAvail + 7) >> 3, *buf);
	// GetBits(&bsi, *bitOffset);

	/* unpack ADIF file header */
	fhADIF = &(psi->fhADIF);
	pce = psi->pce;

	/* verify that first 32 bits of header are "ADIF" */
	if (GetBits(&bsi, 8) != 'A' || GetBits(&bsi, 8) != 'D' || GetBits(&bsi, 8) != 'I' || GetBits(&bsi, 8) != 'F')
		return ERR_AAC_INVALID_ADIF_HEADER;

	/* read ADIF header fields */
	fhADIF->copyBit = GetBits(&bsi, 1);
	if (fhADIF->copyBit) {
		for (i = 0; i < ADIF_COPYID_SIZE; i++)
			fhADIF->copyID[i] = GetBits(&bsi, 8);
	}
	fhADIF->origCopy = GetBits(&bsi, 1);
	fhADIF->home =     GetBits(&bsi, 1);
	fhADIF->bsType =   GetBits(&bsi, 1);
	fhADIF->bitRate =  GetBits(&bsi, 23);
	fhADIF->numPCE =   GetBits(&bsi, 4) + 1;	/* add 1 (so range = [1, 16]) */
	if (fhADIF->bsType == 0)
		fhADIF->bufferFull = GetBits(&bsi, 20);

	/* parse all program config elements */
	for (i = 0; i < fhADIF->numPCE; i++)
		DecodeProgramConfigElement(pce + i, &bsi);

	/* byte align */
	ByteAlignBitstream(&bsi);

	/* update codec info */
	psi->nChans = GetNumChannelsADIF(pce, fhADIF->numPCE);
	psi->sampRateIdx = GetSampleRateIdxADIF(pce, fhADIF->numPCE);

	/* check validity of header */
	if (psi->nChans < 0 || psi->sampRateIdx < 0 || psi->sampRateIdx >= NUM_SAMPLE_RATES)
		return ERR_AAC_INVALID_ADIF_HEADER;

	/* syntactic element fields will be read from bitstream for each element */
	aacDecInfo->prevBlockID = AAC_ID_INVALID;
	aacDecInfo->currBlockID = AAC_ID_INVALID;
	aacDecInfo->currInstTag = -1;

	/* fill in user-accessible data */
	aacDecInfo->bitRate = 0;
	aacDecInfo->nChans = psi->nChans;
	aacDecInfo->sampRate = sampRateTab[psi->sampRateIdx];
	aacDecInfo->profile = pce[0].profile;
	aacDecInfo->sbrEnabled = 0;

	/* update bitstream reader */
	bitsUsed = CalcBitsUsed(&bsi, *buf, *bitOffset);
	*buf += (bitsUsed + *bitOffset) >> 3;
	*bitOffset = (bitsUsed + *bitOffset) & 0x07;
	*bitsAvail -= bitsUsed ;
	if (*bitsAvail < 0)
		return ERR_AAC_INDATA_UNDERFLOW;

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    SetRawBlockParams
 *
 * Description: set internal state variables for decoding a stream of raw data blocks
 *
 * Inputs:      valid AACDecInfo struct
 *              flag indicating source of parameters (from previous headers or passed
 *                explicitly by caller)
 *              number of channels
 *              sample rate
 *              profile ID
 *
 * Outputs:     updated state variables in aacDecInfo
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Notes:       if copyLast == 1, then psi->nChans, psi->sampRateIdx, and
 *                aacDecInfo->profile are not changed (it's assumed that we already
 *                set them, such as by a previous call to UnpackADTSHeader())
 *              if copyLast == 0, then the parameters we passed in are used instead
 **************************************************************************************/
int SetRawBlockParams(AACDecInfo *aacDecInfo, int copyLast, int nChans, int sampRate, int profile)
{
	int idx;
	PSInfoBase *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoBase *)(aacDecInfo->psInfoBase);

	if (!copyLast) {
		aacDecInfo->profile = profile;
		psi->nChans = nChans;
		for (idx = 0; idx < NUM_SAMPLE_RATES; idx++) {
			if (sampRate == sampRateTab[idx]) {
				psi->sampRateIdx = idx;
				break;
			}
		}
		if (idx == NUM_SAMPLE_RATES)
			return ERR_AAC_INVALID_FRAME;
	}
	aacDecInfo->nChans = psi->nChans;
	aacDecInfo->sampRate = sampRateTab[psi->sampRateIdx];

	/* check validity of header */
	if (psi->sampRateIdx >= NUM_SAMPLE_RATES || psi->sampRateIdx < 0 || aacDecInfo->profile != AAC_PROFILE_LC)
		return ERR_AAC_RAWBLOCK_PARAMS;

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    PrepareRawBlock
 *
 * Description: reset per-block state variables for raw blocks (no ADTS/ADIF headers)
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     updated state variables in aacDecInfo
 *
 * Return:      0 if successful, error code (< 0) if error
 **************************************************************************************/
int PrepareRawBlock(AACDecInfo *aacDecInfo)
{
	//PSInfoBase *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	//psi = (PSInfoBase *)(aacDecInfo->psInfoBase);

	/* syntactic element fields will be read from bitstream for each element */
	aacDecInfo->prevBlockID = AAC_ID_INVALID;
	aacDecInfo->currBlockID = AAC_ID_INVALID;
	aacDecInfo->currInstTag = -1;

	/* fill in user-accessible data */
	aacDecInfo->bitRate = 0;
	aacDecInfo->sbrEnabled = 0;

	return ERR_AAC_NONE;
}

/**************************************************************************************
 * Function:    FlushCodec
 *
 * Description: flush internal codec state (after seeking, for example)
 *
 * Inputs:      valid AACDecInfo struct
 *
 * Outputs:     updated state variables in aacDecInfo
 *
 * Return:      0 if successful, error code (< 0) if error
 *
 * Notes:       only need to clear data which is persistent between frames
 *                (such as overlap buffer)
 **************************************************************************************/
int FlushCodec(AACDecInfo *aacDecInfo)
{
	PSInfoBase *psi;

	/* validate pointers */
	if (!aacDecInfo || !aacDecInfo->psInfoBase)
		return ERR_AAC_NULL_POINTER;
	psi = (PSInfoBase *)(aacDecInfo->psInfoBase);

	ClearBuffer(psi->overlap, AAC_MAX_NCHANS * AAC_MAX_NSAMPS * sizeof(int));
	ClearBuffer(psi->prevWinShape, AAC_MAX_NCHANS * sizeof(int));

	return ERR_AAC_NONE;
}
