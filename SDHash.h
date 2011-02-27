/*
    SDHash - small adhoc filesystem for Arduinos
    Copyright (C) 2011  Shuning Bian

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

	Contact: freespace@gmail.com
*/

#ifndef SDHASH_H
#define SDHASH_H
#include "WProgram.h"

#include <stdint.h>

#include "utility/Sd2Card.h"

enum {
	SDH_OK,
	SDH_ERR_FILE_NOT_FOUND,
	SDH_ERR_NO_SPACE,
	SDH_ERR_FILENAME,
	SDH_ERR_FILE_EXISTS,
	SDH_ERR_WRONG_SEGMENT_TYPE,
	SDH_ERR_INVALID_ARGUMENT,
	SDH_ERR_MISSIG_SEGMENT,
	SDH_ERR_SD, // error occur relating to Sd2Card
	SDH_ERR_CARD, // something is wrong about the card
};

enum {
	kSDHashJournalCreate = 'c',
	kSDHashJournalDelete = 'd',
};

typedef uint32_t SDHAddress;
typedef uint32_t SDHFilehandle;
typedef uint16_t SDHSegmentCount;
typedef uint16_t SDHDataSize;
typedef uint32_t SDHBucketCount;

typedef struct {
	uint8_t version;
	SDHBucketCount buckets;
} HashInfo;

typedef struct {
	SDHAddress segment0_addr;
	SDHDataSize length;
} SegmentInfo;

typedef struct {
	SDHFilehandle hash;
	SDHSegmentCount segments_count;
} Segment0Info;	

typedef Segment0Info FileInfo;

class SDHashClass {
	private:
		Sd2Card _card;
		HashInfo _hashInfo;

		bool _validCard;

	public:
		static SDHFilehandle filehandle(char *str);
		static SDHFilehandle filehandle(uint8_t *buf, size_t len);

		static uint32_t fnv(uint8_t *buf, size_t len, uint32_t hval); 

		SDHashClass(): _validCard(false){};

		uint8_t begin();
		bool validCard() {return _validCard;}
		Sd2Card *card() {return &_card;}

		/**
		 * following returns 0 on success, error code otherwise
		 */ 

		/** 
		 * Zero the blocks starting from startblock and continuing on for
		 * count blocks.
		 * 
		 */
		uint8_t zero(SDHAddress startblock, uint16_t count);
		
		/**
		 * zeros the first block, so card is no longer a valid SDHash card
		 */ 
		uint8_t zeroMagic();

		uint8_t sdErrorCode() { return _card.errorCode(); }

		/**
		 * Creates a file with or without data. 
		 *
		 * If file already exists, SDH_ERR_FILE_EXISTS is returned.
		 */ 
		uint8_t createFile(SDHFilehandle fh, const char *filename, uint8_t *data, SDHDataSize len);
		uint8_t createFile(SDHFilehandle fh, const char *filename);
	
		/**
		 * writes file info into given FileInfo struct which can be
		 * null if you just want to know if the file exists.
		 *
		 * SDH_ERR_FILE_NOT_FOUND is returned if the file can not be
		 * found, and there is space in the hash table.
		 *
		 * addr, if not NULL, contains the block addr of the first
		 * segment if it is found, the block addr of the first free
		 * segment if not found.
		 *
		 * If the bucket is full, and we can't find the file, then
		 * SDH_ERR_NO_SPACE is returned.
		 */
		uint8_t statFile(SDHFilehandle fh, FileInfo *finfo, SDHAddress *addr);
		uint8_t statSeg0(SDHAddress addr, FileInfo *finfo);
		uint8_t statSeg(SDHAddress addr, SegmentInfo *sinfo);
		/**
		 * finds segment with seg0addr in their meta, starting at 
		 * addr. If found, SDH_OK is returned annd addr contains
		 * the address of where the segment was found. As soon as this
		 * method encounters a free segment SDH_ERR_FILE_NOT_FOUND is 
		 * returned.
		 *
		 * Passing in seg0addr of 0 effectively finds the first available
		 * free segment since no segment 0 is reserved.
		 *
		 * This can not be used to find seg0s.
		 */
		uint8_t findSeg(SDHAddress seg0addr, SDHAddress *addr);

		/**
		 * Same as above, but just requires a filehandle and a segment number.
		 * Segment numbers start from 0...n-1, where n is the number of segments
		 *
		 */
		uint8_t findSeg(SDHFilehandle fh, SDHSegmentCount segmentNumber, SDHAddress *addr); 

		uint8_t appendFile(SDHFilehandle fh, uint8_t *data, SDHDataSize len);

		/**
		 * Reads len bytes from into dest, optionally offset into the file
		 *
		 * returns SDH_OK on success, and len will hold the number of
		 * bytes in dest that wasn't used.  e.g. len of 0 will indicate
		 * dest was completely filled.
		 * 
		 */
		uint8_t readFile(SDHFilehandle fh, uint32_t offset, uint8_t *dest, SDHDataSize *len);

		/**
		 * Replaces the data of the n-th segment. len can not be greater tha 512, but
		 * this condition is not checked by the library.
		 *
		 * if segNumber is 0, SDH_ERR_INVALID_ARGUMENT is returned since segment 0
		 * is not a valid data segment
		 */ 
		uint8_t replaceSegment(SDHFilehandle fh, SDHSegmentCount segNumber, uint8_t *data, SDHDataSize len);
		/**
		 * Sets a segment's length to 0 effectively deleting it from the file. Such segments are still registered
		 * with segment zero, and can not be removed without destroying the hash chain. Space used by these segments
		 * can be reclaimed by defragging the hash table.
		 */ 
		uint8_t truncateSegment(SDHFilehandle fh, SDHSegmentCount segNumber);
		uint8_t deleteFile(SDHFilehandle fh);

		/**
		 * Truncates the file by count number of segments. If truncation would result in truncation
		 * of segment 0 as well, SDH_ERR_INVALID_ARGUMENT is returned.
		 */ 
		uint8_t truncateFile(SDHFilehandle fh, SDHSegmentCount count);
	private:
		bool _getHashInfo();
		SDHAddress _foldHash(uint32_t hash);
		uint32_t _incHash(uint32_t hash);
		uint8_t _writeSegment(SDHAddress seg0addr, SDHAddress addr, uint8_t *data, SDHDataSize len);
		uint8_t _updateSeg0SegmentsCount(SDHAddress seg0addr, SDHSegmentCount segments_count);
		uint8_t _appendLog(uint8_t type, SDHAddress seg0addr);
		uint8_t _statSeg(SDHAddress addr, uint8_t type, void *info);
};

extern SDHashClass SDHash;

inline uint8_t SDHashClass::createFile(SDHFilehandle fh, const char *filename) {
	return createFile(fh, filename, NULL, 0);
}

inline SDHFilehandle SDHashClass::filehandle(char *str) {
///	Serial.print(">");Serial.print(str);Serial.println("<");
	return filehandle((uint8_t*)str, strlen(str));
}

inline SDHFilehandle SDHashClass::filehandle(uint8_t *buf, size_t len) {
	return fnv(buf, len, 0);
}

inline uint8_t SDHashClass::truncateSegment(SDHFilehandle fh, SDHSegmentCount segNumber) {
	return replaceSegment(fh, segNumber, NULL, 0);
}
#endif
