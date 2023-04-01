/*
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011 - 2016, Yann Collet.
 * BSD 2 - Clause License (http://www.opensource.org/licenses/bsd - license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

/*-************************************
 *	Dependencies
 **************************************/
#include <linux/lz4.h>
#include "lz4defs.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>

#include "lz4armv8/lz4accel.h"

/*-*****************************
 *	Decompression functions
 *******************************/
/* LZ4_decompress_generic() :
 * This generic decompression function cover all use cases.
 * It shall be instantiated several times, using different sets of directives
 * Note that it is important this generic function is really inlined,
 * in order to remove useless branches during compilation optimization.
 */
static FORCE_INLINE int __LZ4_decompress_generic(
	 const char * const source,
	 char * const dest,
	 const BYTE * ip,
	 BYTE * op,
	 int inputSize,
		/*
		 * If endOnInput == endOnInputSize,
		 * this value is the max size of Output Buffer.
		 */
	 int outputSize,
	 /* endOnOutputSize, endOnInputSize */
	 int endOnInput,
	 /* full, partial */
	 int partialDecoding,
	 /* only used if partialDecoding == partial */
	 int targetOutputSize,
	 /* noDict, withPrefix64k, usingExtDict */
	 int dict,
	 /* == dest when no prefix */
	 const BYTE * const lowPrefix,
	 /* only if dict == usingExtDict */
	 const BYTE * const dictStart,
	 /* note : = 0 if noDict */
	 const size_t dictSize
	 )
{
	/* Local Variables */
	const BYTE * const iend = source + inputSize;

	BYTE * const oend = op + outputSize;
	BYTE *cpy;
	BYTE *oexit = op + targetOutputSize;
	const BYTE * const lowLimit = lowPrefix - dictSize;

	const BYTE * const dictEnd = (const BYTE *)dictStart + dictSize;
	static const unsigned int dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };
	static const int dec64table[] = { 0, 0, 0, -1, 0, 1, 2, 3 };

	const int safeDecode = (endOnInput == endOnInputSize);
	const int checkOffset = ((safeDecode) && (dictSize < (int)(64 * KB)));

	/* Special cases */
	/* targetOutputSize too high => decode everything */
	if ((partialDecoding) && (oexit > oend - MFLIMIT))
		oexit = oend - MFLIMIT;

	/* Empty output buffer */
	if ((endOnInput) && (unlikely(outputSize == 0)))
		return ((inputSize == 1) && (*ip == 0)) ? 0 : -1;

	if ((!endOnInput) && (unlikely(outputSize == 0)))
		return (*ip == 0 ? 1 : -1);

	/* Main Loop : decode sequences */
	while (1) {
		size_t length;
		const BYTE *match;
		size_t offset;

		/* get literal length */
		unsigned int const token = *ip++;

		length = token>>ML_BITS;

		if (length == RUN_MASK) {
			unsigned int s;

			do {
				s = *ip++;
				length += s;
			} while (likely(endOnInput
				? ip < iend - RUN_MASK
				: 1) & (s == 255));

			if ((safeDecode)
				&& unlikely(
					(size_t)(op + length) < (size_t)(op))) {
				/* overflow detection */
				goto _output_error;
			}
			if ((safeDecode)
				&& unlikely(
					(size_t)(ip + length) < (size_t)(ip))) {
				/* overflow detection */
				goto _output_error;
			}
		}

		/* copy literals */
		cpy = op + length;
		if (((endOnInput) && ((cpy > (partialDecoding ? oexit : oend - MFLIMIT))
			|| (ip + length > iend - (2 + 1 + LASTLITERALS))))
			|| ((!endOnInput) && (cpy > oend - WILDCOPYLENGTH))) {
			if (partialDecoding) {
				if (cpy > oend) {
					/*
					 * Error :
					 * write attempt beyond end of output buffer
					 */
					goto _output_error;
				}
				if ((endOnInput)
					&& (ip + length > iend)) {
					/*
					 * Error :
					 * read attempt beyond
					 * end of input buffer
					 */
					goto _output_error;
				}
			} else {
				if ((!endOnInput)
					&& (cpy != oend)) {
					/*
					 * Error :
					 * block decoding must
					 * stop exactly there
					 */
					goto _output_error;
				}
				if ((endOnInput)
					&& ((ip + length != iend)
					|| (cpy > oend))) {
					/*
					 * Error :
					 * input must be consumed
					 */
					goto _output_error;
				}
			}

			memcpy(op, ip, length);
			ip += length;
			op += length;
			
			/* Necessarily EOF when !partialDecoding.
			 * When partialDecoding, it is EOF if we've either
			 * filled the output buffer or
			 * can't proceed with reading an offset for following match.
			 */
			if (!partialDecoding || (cpy == oend) || (ip >= (iend - 2)))
				break;
		}

		LZ4_wildCopy(op, ip, cpy);
		ip += length;
		op = cpy;

		/* get offset */
		offset = LZ4_readLE16(ip);
		ip += 2;
		match = op - offset;

		if ((checkOffset) && (unlikely(match < lowLimit))) {
			/* Error : offset outside buffers */
			goto _output_error;
		}

		/* costs ~1%; silence an msan warning when offset == 0 */
		LZ4_write32(op, (U32)offset);

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			unsigned int s;

			do {
				s = *ip++;

				if ((endOnInput) && (ip > iend - LASTLITERALS))
					goto _output_error;

				length += s;
			} while (s == 255);

			if ((safeDecode)
				&& unlikely(
					(size_t)(op + length) < (size_t)op)) {
				/* overflow detection */
				goto _output_error;
			}
		}

		length += MINMATCH;

		/* check external dictionary */
		if ((dict == usingExtDict) && (match < lowPrefix)) {
			if (unlikely(op + length > oend - LASTLITERALS)) {
				/* doesn't respect parsing restriction */
				goto _output_error;
			}

			if (length <= (size_t)(lowPrefix - match)) {
				/*
				 * match can be copied as a single segment
				 * from external dictionary
				 */
				memmove(op, dictEnd - (lowPrefix - match),
					length);
				op += length;
			} else {
				/*
				 * match encompass external
				 * dictionary and current block
				 */
				size_t const copySize = (size_t)(lowPrefix - match);
				size_t const restSize = length - copySize;

				memcpy(op, dictEnd - copySize, copySize);
				op += copySize;

				if (restSize > (size_t)(op - lowPrefix)) {
					/* overlap copy */
					BYTE * const endOfMatch = op + restSize;
					const BYTE *copyFrom = lowPrefix;

					while (op < endOfMatch)
						*op++ = *copyFrom++;
				} else {
					memcpy(op, lowPrefix, restSize);
					op += restSize;
				}
			}

			continue;
		}

		/* copy match within block */
		cpy = op + length;

		if (unlikely(offset < 8)) {
			const int dec64 = dec64table[offset];

			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += dec32table[offset];
			memcpy(op + 4, match, 4);
			match -= dec64;
		} else {
			LZ4_copy8(op, match);
			match += 8;
		}

		op += 8;

		if (unlikely(cpy > oend - 12)) {
			BYTE * const oCopyLimit = oend - (WILDCOPYLENGTH - 1);

			if (cpy > oend - LASTLITERALS) {
				/*
				 * Error : last LASTLITERALS bytes
				 * must be literals (uncompressed)
				 */
				goto _output_error;
			}

			if (op < oCopyLimit) {
				LZ4_wildCopy(op, match, oCopyLimit);
				match += oCopyLimit - op;
				op = oCopyLimit;
			}

			while (op < cpy)
				*op++ = *match++;
		} else {
			LZ4_copy8(op, match);

			if (length > 16)
				LZ4_wildCopy(op + 8, match + 8, cpy);
		}

		op = cpy; /* correction */
	}

	/* end of decoding */
	if (endOnInput) {
		/* Nb of output bytes decoded */
		return (int) (((char *)op) - dest);
	} else {
		/* Nb of input bytes read */
		return (int) (((const char *)ip) - source);
	}

	/* Overflow error detected */
_output_error:
	return -1;
}

static FORCE_INLINE int LZ4_decompress_generic(
	 const char * const src,
	 char * const dst,
	 int srcSize,
		/*
		 * If endOnInput == endOnInputSize,
		 * this value is `dstCapacity`
		 */
	 int outputSize,
	 /* endOnOutputSize, endOnInputSize */
	 endCondition_directive endOnInput,
	 /* full, partial */
	 earlyEnd_directive partialDecoding,
	 /* noDict, withPrefix64k, usingExtDict */
	 dict_directive dict,
	 /* always <= dst, == dst when no prefix */
	 const BYTE * const lowPrefix,
	 /* only if dict == usingExtDict */
	 const BYTE * const dictStart,
	 /* note : = 0 if noDict */
	 const size_t dictSize
	 )
{
	return __LZ4_decompress_generic(src, dst, (const BYTE *)src, (BYTE *)dst, srcSize, outputSize, endOnInput, partialDecoding, dict, lowPrefix, dictStart, dictSize);
}

int LZ4_decompress_safe(const char *source, char *dest,
	int compressedSize, int maxDecompressedSize)
{
	return LZ4_decompress_generic(source, dest, compressedSize,
		maxDecompressedSize, endOnInputSize, full, 0,
		noDict, (BYTE *)dest, NULL, 0);
}

int LZ4_decompress_safe_partial(const char *source, char *dest,
	int compressedSize, int targetOutputSize, int maxDecompressedSize)
{
	return LZ4_decompress_generic(source, dest, compressedSize,
		maxDecompressedSize, endOnInputSize, partial,
		targetOutputSize, noDict, (BYTE *)dest, NULL, 0);
}

int LZ4_decompress_fast(const char *source, char *dest, int originalSize)
{
	return LZ4_decompress_generic(source, dest, 0, originalSize,
		endOnOutputSize, full, 0, withPrefix64k,
		(BYTE *)(dest - 64 * KB), NULL, 64 * KB);
}

int LZ4_setStreamDecode(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *dictionary, int dictSize)
{
	LZ4_streamDecode_t_internal *lz4sd = (LZ4_streamDecode_t_internal *) LZ4_streamDecode;

	lz4sd->prefixSize = (size_t) dictSize;
	lz4sd->prefixEnd = (const BYTE *) dictionary + dictSize;
	lz4sd->externalDict = NULL;
	lz4sd->extDictSize	= 0;
	return 1;
}

/*
 * *_continue() :
 * These decoding functions allow decompression of multiple blocks
 * in "streaming" mode.
 * Previously decoded blocks must still be available at the memory
 * position where they were decoded.
 * If it's not possible, save the relevant part of
 * decoded data into a safe buffer,
 * and indicate where it stands using LZ4_setStreamDecode()
 */
int LZ4_decompress_safe_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int compressedSize, int maxOutputSize)
{
	LZ4_streamDecode_t_internal *lz4sd = &LZ4_streamDecode->internal_donotuse;
	int result;

	if (lz4sd->prefixEnd == (BYTE *)dest) {
		result = LZ4_decompress_generic(source, dest,
			compressedSize,
			maxOutputSize,
			endOnInputSize, full, 0,
			usingExtDict, lz4sd->prefixEnd - lz4sd->prefixSize,
			lz4sd->externalDict,
			lz4sd->extDictSize);

		if (result <= 0)
			return result;

		lz4sd->prefixSize += result;
		lz4sd->prefixEnd	+= result;
	} else {
		lz4sd->extDictSize = lz4sd->prefixSize;
		lz4sd->externalDict = lz4sd->prefixEnd - lz4sd->extDictSize;
		result = LZ4_decompress_generic(source, dest,
			compressedSize, maxOutputSize,
			endOnInputSize, full, 0,
			usingExtDict, (BYTE *)dest,
			lz4sd->externalDict, lz4sd->extDictSize);
		if (result <= 0)
			return result;
		lz4sd->prefixSize = result;
		lz4sd->prefixEnd	= (BYTE *)dest + result;
	}

	return result;
}

int LZ4_decompress_fast_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int originalSize)
{
	LZ4_streamDecode_t_internal *lz4sd = &LZ4_streamDecode->internal_donotuse;
	int result;

	if (lz4sd->prefixEnd == (BYTE *)dest) {
		result = LZ4_decompress_generic(source, dest, 0, originalSize,
			endOnOutputSize, full, 0,
			usingExtDict,
			lz4sd->prefixEnd - lz4sd->prefixSize,
			lz4sd->externalDict, lz4sd->extDictSize);

		if (result <= 0)
			return result;

		lz4sd->prefixSize += originalSize;
		lz4sd->prefixEnd	+= originalSize;
	} else {
		lz4sd->extDictSize = lz4sd->prefixSize;
		lz4sd->externalDict = lz4sd->prefixEnd - lz4sd->extDictSize;
		result = LZ4_decompress_generic(source, dest, 0, originalSize,
			endOnOutputSize, full, 0,
			usingExtDict, (BYTE *)dest,
			lz4sd->externalDict, lz4sd->extDictSize);
		if (result <= 0)
			return result;
		lz4sd->prefixSize = originalSize;
		lz4sd->prefixEnd	= (BYTE *)dest + originalSize;
	}

	return result;
}

/*
 * Advanced decoding functions :
 * *_usingDict() :
 * These decoding functions work the same as "_continue" ones,
 * the dictionary must be explicitly provided within parameters
 */
static FORCE_INLINE int LZ4_decompress_usingDict_generic(const char *source,
	char *dest, int compressedSize, int maxOutputSize, int safe,
	const char *dictStart, int dictSize)
{
	if (dictSize == 0)
		return LZ4_decompress_generic(source, dest,
			compressedSize, maxOutputSize, safe, full, 0,
			noDict, (BYTE *)dest, NULL, 0);
	if (dictStart + dictSize == dest) {
		if (dictSize >= (int)(64 * KB - 1))
			return LZ4_decompress_generic(source, dest,
				compressedSize, maxOutputSize, safe, full, 0,
				withPrefix64k, (BYTE *)dest - 64 * KB, NULL, 0);
		return LZ4_decompress_generic(source, dest, compressedSize,
			maxOutputSize, safe, full, 0, noDict,
			(BYTE *)dest - dictSize, NULL, 0);
	}
	return LZ4_decompress_generic(source, dest, compressedSize,
		maxOutputSize, safe, full, 0, usingExtDict,
		(BYTE *)dest, (const BYTE *)dictStart, dictSize);
}

int LZ4_decompress_safe_usingDict(const char *source, char *dest,
	int compressedSize, int maxOutputSize,
	const char *dictStart, int dictSize)
{
	return LZ4_decompress_usingDict_generic(source, dest,
		compressedSize, maxOutputSize, 1, dictStart, dictSize);
}

int LZ4_decompress_fast_usingDict(const char *source, char *dest,
	int originalSize, const char *dictStart, int dictSize)
{
	return LZ4_decompress_usingDict_generic(source, dest, 0,
		originalSize, 0, dictStart, dictSize);
}

ssize_t LZ4_arm64_decompress_safe_partial(const void *source,
			      void *dest,
			      size_t inputSize,
			      size_t outputSize,
			      bool dip)
{
        uint8_t         *dstPtr = dest;
        const uint8_t   *srcPtr = source;
        ssize_t         ret;

#ifdef __ARCH_HAS_LZ4_ACCELERATOR
        /* Go fast if we can, keeping away from the end of buffers */
        if (outputSize > LZ4_FAST_MARGIN && inputSize > LZ4_FAST_MARGIN && lz4_decompress_accel_enable()) {
                ret = lz4_decompress_asm(&dstPtr, dest,
                                         dest + outputSize - LZ4_FAST_MARGIN,
                                         &srcPtr,
                                         source + inputSize - LZ4_FAST_MARGIN,
                                         dip);
                if (ret)
                        return -EIO;
        }
#endif
        /* Finish in safe */
	return __LZ4_decompress_generic(source, dest, srcPtr, dstPtr, inputSize, outputSize, endOnInputSize, partial_decode, noDict, (BYTE *)dest, NULL, 0);
}

ssize_t LZ4_arm64_decompress_safe(const void *source,
			      void *dest,
			      size_t inputSize,
			      size_t outputSize,
			      bool dip)
{
        uint8_t         *dstPtr = dest;
        const uint8_t   *srcPtr = source;
        ssize_t         ret;

#ifdef __ARCH_HAS_LZ4_ACCELERATOR
        /* Go fast if we can, keeping away from the end of buffers */
        if (outputSize > LZ4_FAST_MARGIN && inputSize > LZ4_FAST_MARGIN && lz4_decompress_accel_enable()) {
                ret = lz4_decompress_asm(&dstPtr, dest,
                                         dest + outputSize - LZ4_FAST_MARGIN,
                                         &srcPtr,
                                         source + inputSize - LZ4_FAST_MARGIN,
                                         dip);
                if (ret)
                        return -EIO;
        }
#endif
        /* Finish in safe */
	return __LZ4_decompress_generic(source, dest, srcPtr, dstPtr, inputSize, outputSize, endOnInputSize, decode_full_block, noDict, (BYTE *)dest, NULL, 0);
}

#ifndef STATIC
EXPORT_SYMBOL(LZ4_decompress_safe);
EXPORT_SYMBOL(LZ4_decompress_safe_partial);
EXPORT_SYMBOL(LZ4_decompress_fast);
EXPORT_SYMBOL(LZ4_setStreamDecode);
EXPORT_SYMBOL(LZ4_decompress_safe_continue);
EXPORT_SYMBOL(LZ4_decompress_fast_continue);
EXPORT_SYMBOL(LZ4_decompress_safe_usingDict);
EXPORT_SYMBOL(LZ4_decompress_fast_usingDict);
EXPORT_SYMBOL(LZ4_arm64_decompress_safe);
EXPORT_SYMBOL(LZ4_arm64_decompress_safe_partial);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 decompressor");
#endif
