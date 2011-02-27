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
#include "SdFatUtil.h"
#include "SDHash.h"
///#define SERIAL_DEBUG

#ifdef SERIAL_DEBUG
#define Serial_print(x,args...) Serial.print(x, ## args)
#define Serial_println(x,args...) Serial.println(x, ## args)
#else
#define Serial_print(x,...) 
#define Serial_println(x,...) 
#endif

// when we target non-little endian platforms, _BSWAP*
// can be defined to actually to something

#define _BSWAP32(x) x
#define _BSWAP16(x) x

#define STEP(f) (f%2?1:-1)

#define kSDHashLogFilename "__LOG"
#define kSDHashLogFilenameHash 0x00428ef4
#define kSDHashHiddenFilenamePrefix "__"
#define kSDHashHiddenFilenamePrefixLen 2

static const uint8_t kSDHashMagic[5] = {0xae, 'h', 'a', 's', 'h'};
// magic + 1 byte of version +  bytes of bucket count
#define kSDHashHeaderSize (sizeof kSDHashMagic + 1 + sizeof(SDHBucketCount))

#define kSDHashMaxFilenameLength (23)
// type + hash + segment count
#define kSDHashSegment0MetaHeaderSize (1 + sizeof(SDHFilehandle) + sizeof(SDHSegmentCount))

// header + filename + 1 padding
#define kSDHashSegment0MetaSize (kSDHashSegment0MetaHeaderSize + kSDHashMaxFilenameLength + 1)

// type + seg 0 addr + length
#define kSDHashSegmentMetaSize (1 + sizeof(SDHAddress) + sizeof(SDHDataSize))

#define kSDHashSegmentDataSize (512-kSDHashSegmentMetaSize)


enum {
	kSDHashFreeSegment = 0x00,
	kSDHashSegment0 = 0x01,
	kSDHashSegment = 0x02,
};

uint32_t SDHashClass::fnv(uint8_t *buf, size_t len, uint32_t hval) {
	Serial_print("fnv:0x");
	Serial_print(hval, HEX);
	Serial_print(" .. ");
	uint8_t *end = buf+len;
	for(;buf < end;++buf) {
		hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
		hval ^= (uint32_t)*buf;
	}
	Serial_print("0x");
	Serial_println(hval, HEX);

	return hval;
}

uint8_t SDHashClass::zeroMagic() {
	uint8_t zero[1] = {0x00};
	if (!_card.writeBlock(0, zero, sizeof zero)) {
		return SDH_ERR_SD;
	}
	
	return SDH_OK;
}

uint8_t SDHashClass::zero(SDHAddress startblock, uint16_t count) {
	uint8_t zero[1] = {kSDHashFreeSegment};
	_card.writeStart(startblock, count);
	for (; count>0; count-=1) {
		if (!_card.writeData(zero, sizeof zero, 0)) return SDH_ERR_SD;
		if (!_card.writeDataPadding(512-sizeof zero)) return SDH_ERR_SD;
	}

	if (!_card.writeStop()) {
		return SDH_ERR_SD;
	}

	return SDH_OK;
}

uint8_t SDHashClass::begin() {
	_validCard = false;
	pinMode(10, OUTPUT); 
	_card.init();

	// stop any reads and writes that were in progress
	// when serial monitor started/stopped, or if we got
	// reset while the SD card didn't
	_card.writeStop();
	_card.readEnd();

	if (_getHashInfo()) {
		_validCard = true;
		Serial_print("found SDHash, version=");
		Serial_print(_hashInfo.version, DEC);
		Serial_print(" buckets=");
		Serial_println(_hashInfo.buckets);

		// make sure our buckets count is smaller than cardsize
		// otherwise things are going to go haywire
		if (_hashInfo.buckets > _card.cardSize()) {
			Serial_println("card isn't big enough");
			return SDH_ERR_CARD;
		}

		if (statFile(kSDHashLogFilenameHash, NULL, NULL) == SDH_ERR_FILE_NOT_FOUND) {
			return SDHash.createFile(kSDHashLogFilenameHash, kSDHashLogFilename);
		}
		return SDH_OK;
	} else {
		_hashInfo.version = 1;
		_hashInfo.buckets = _card.cardSize();

		if (_hashInfo.buckets) {
			uint8_t header[kSDHashHeaderSize];
			memcpy(header, kSDHashMagic, sizeof kSDHashMagic);
			header[sizeof kSDHashMagic] = _hashInfo.version;

			_hashInfo.buckets = _BSWAP32(_hashInfo.buckets);
			memcpy(header + sizeof kSDHashMagic + 1, &_hashInfo.buckets, sizeof _hashInfo.buckets);

			if (_card.writeBlock(0, header, sizeof header)) {
				Serial_println("card marked as SDHash");
				_validCard = true;
			} else {
				Serial_print("failed to write header=0x");
				Serial_println(_card.errorCode(), HEX);
				return SDH_ERR_SD;
			}

			SDHash.deleteFile(kSDHashLogFilenameHash);
			return SDHash.createFile(kSDHashLogFilenameHash, kSDHashLogFilename);
		} else {
			Serial_println("card has no buckets?");
			return SDH_ERR_CARD;
		}
	}
}

uint8_t SDHashClass::createFile(SDHFilehandle fh, const char *filename, uint8_t *data, SDHDataSize len) {
	SDHAddress addr;
	if (statFile(fh, NULL, &addr) == SDH_ERR_FILE_NOT_FOUND) {
		Serial_print("addr=");
		Serial_println(addr);

		uint8_t namelen = strlen(filename);
		char name_padding = kSDHashMaxFilenameLength - namelen;
		if (name_padding <= 0) return SDH_ERR_FILENAME;
		name_padding+=1;

		if(!_card.writeStart(addr, 1)) return SDH_ERR_SD;
		
		uint8_t ofs = 0;
		uint8_t type = kSDHashSegment0;

		if(!_card.writeData(&type, sizeof type, ofs)) return SDH_ERR_SD;
		ofs+= sizeof type;

		if(!_card.writeData((uint8_t*)&fh, sizeof fh, ofs)) return SDH_ERR_SD;
		ofs += sizeof fh;

		uint16_t seg_count = _BSWAP16(1);
		if(!_card.writeData((uint8_t*)&seg_count, sizeof seg_count, ofs)) return SDH_ERR_SD;
		ofs += sizeof seg_count;

		if(!_card.writeData((uint8_t*)filename, namelen, ofs)) return SDH_ERR_SD;
		ofs += namelen;

		for(uint8_t cnt = 0; cnt < name_padding; ++cnt) {
			if(!_card.writeData((uint8_t*)&name_padding, sizeof name_padding, ofs)) return SDH_ERR_SD;
			ofs += sizeof name_padding;
		}
		
		if(!_card.writeDataPadding(512 - ofs)) return SDH_ERR_SD;
		
		if(!_card.writeStop()) return SDH_ERR_SD;

		if (namelen >=kSDHashHiddenFilenamePrefixLen) {
			if (memcmp(filename, kSDHashHiddenFilenamePrefix, kSDHashHiddenFilenamePrefixLen)) {
				uint8_t ret = _appendLog(kSDHashJournalCreate, addr);
				if (ret != SDH_OK) return ret;
			}
		}

		if (data && len) return appendFile(fh, data, len); 

		return SDH_OK;
	}
	else return SDH_ERR_FILE_EXISTS;
}

uint8_t SDHashClass::appendFile(SDHFilehandle fh, uint8_t* data, SDHDataSize len) {
	if (data == NULL || len < 1) return SDH_ERR_INVALID_ARGUMENT;

	FileInfo finfo;
	SDHAddress seg0addr;
	uint8_t ret = statFile(fh, &finfo, &seg0addr);
	if (ret != SDH_OK) return ret;

	// bring the hash 'up to date'
	for(uint16_t cnt = 0; cnt < finfo.segments_count; ++cnt) {
		fh = _incHash(fh);
	}

	uint16_t seg_len;
	do {
		seg_len = min(kSDHashSegmentDataSize, len);
		SDHAddress seg_addr = _foldHash(fh);

		ret = findSeg(0, &seg_addr);
		if (ret == SDH_ERR_FILE_NOT_FOUND) {
			ret = _writeSegment(seg0addr, seg_addr, data, seg_len);
			if (ret != SDH_OK) return ret;

			len -= seg_len;
			data += seg_len;
			fh = _incHash(fh);
			finfo.segments_count += 1;
		} else return ret;
	} while (len > 0);

	_updateSeg0SegmentsCount(seg0addr, finfo.segments_count);
	
	return SDH_OK;
}

uint8_t SDHashClass::replaceSegment(SDHFilehandle fh, uint16_t segNumber, uint8_t *data, SDHDataSize len) {
	if (segNumber == 0) return SDH_ERR_INVALID_ARGUMENT;

	SDHAddress seg_addr;
	uint8_t ret = findSeg(fh, segNumber, &seg_addr);
	if (ret == SDH_OK) {
		SDHAddress seg0addr;
		ret = statFile(fh, NULL, &seg0addr);
		if (ret == SDH_OK) {
			return _writeSegment(seg0addr, seg_addr, data, len);
		} else return ret;
	} else return ret;
}

uint8_t SDHashClass::deleteFile(SDHFilehandle fh) {
	FileInfo finfo;
	SDHAddress seg0addr;
	uint8_t type[1] = {kSDHashFreeSegment};

	uint8_t ret = statFile(fh, &finfo, &seg0addr);
	if (ret != SDH_OK) return ret;

	// check to see if this is a hidden file
	uint8_t prefix[kSDHashHiddenFilenamePrefixLen];
	if (!_card.readData(seg0addr, kSDHashSegment0MetaHeaderSize,sizeof prefix, prefix)) {
		return SDH_ERR_SD;
	}

	if (memcmp(prefix, kSDHashHiddenFilenamePrefix, sizeof prefix)) {
		// if it ISN'T then append operation to the log
		ret = _appendLog(kSDHashJournalDelete, seg0addr);
		if (ret != SDH_OK) return ret;
	}

	if (!_card.writeBlock(seg0addr, type, sizeof type)) {
		return SDH_ERR_SD;
	}

	finfo.segments_count-=1;

	SDHAddress seg_addr;
	while(finfo.segments_count) {
		fh = _incHash(fh);
		seg_addr = _foldHash(fh);
		ret = findSeg(seg0addr, &seg_addr);

		if (ret == SDH_OK) {
///			Serial_print("addr=");
///			Serial_println(seg_addr);
			if (!_card.writeBlock(seg_addr, type, sizeof type)) return SDH_ERR_SD;
		}
		// if a segment isn't found, don't stop. Otherwise a single
		// missing segment could lead to a whole bunch of zombie ones
		else if (ret != SDH_ERR_FILE_NOT_FOUND) return ret;

		finfo.segments_count-=1;
	}

	return SDH_OK;
}

uint8_t SDHashClass::readFile(SDHFilehandle fh, uint32_t offset, uint8_t *dest, uint16_t *len) {
	FileInfo finfo;
	SDHAddress seg0addr;
	uint8_t ret;

	ret = statFile(fh, &finfo, &seg0addr);
	if (ret != SDH_OK) return ret;
	
	// the first segment doesn't hold data, so skip it
	for (finfo.segments_count-=1; finfo.segments_count; finfo.segments_count-=1) {
		fh = _incHash(fh);
		SDHAddress addr = _foldHash(fh);

		ret = findSeg(seg0addr, &addr);
		if (ret == SDH_OK) {
			SegmentInfo sinfo;
			ret = statSeg(addr, &sinfo);
			if (ret != SDH_OK) return ret;

			if (offset > sinfo.length) {
				// offset is past this segment, break out
				// and do the next segment
				offset -= sinfo.length;
			} else {
				// offset is inside this segment, so do a read
				// keeping in mind to skip the metadata
				uint16_t bytesRead = min(sinfo.length-offset, *len);
				if(!_card.readData(addr, kSDHashSegmentMetaSize+offset, bytesRead, dest)) return SDH_ERR_SD;
				if (bytesRead < *len) {
					// reading this segment wasn't enough to fill dest.
					// We need to read into one or more subsequent
					// segments, and always at their offset 0
					dest += bytesRead;
					*len -= bytesRead;
					offset = 0;
				} else {
					// we fulfilled the requirements of len,
					// nothing left to do
					*len = 0;
					return SDH_OK;
				}
			}
		} else if (ret == SDH_ERR_FILE_NOT_FOUND) return SDH_ERR_MISSIG_SEGMENT;
		else return ret;
	}

	return SDH_OK;
}

uint8_t SDHashClass::findSeg(SDHAddress seg0addr, SDHAddress *addr) {
	SDHAddress addr0 = *addr;
	SegmentInfo sinfo;
	uint8_t ret;

	do {
		ret = statSeg(*addr, &sinfo);
		if (ret == SDH_OK) {
			if (sinfo.segment0_addr == seg0addr) return SDH_OK;
		} else if (ret != SDH_ERR_WRONG_SEGMENT_TYPE) return ret;

		*addr+=STEP(addr0);
	} while (addr0 != *addr);

	return SDH_ERR_NO_SPACE;
}

uint8_t SDHashClass::findSeg(SDHFilehandle fh, uint16_t segmentNumber, SDHAddress *addr) {
	uint8_t ret = statFile(fh, NULL, addr);
	if (ret == SDH_OK) {
		if (segmentNumber == 0) SDH_OK;
		else {
			SDHAddress seg0addr = *addr;
			while(segmentNumber) {
				fh = _incHash(fh);
				segmentNumber -= 1;
			}

			*addr = _foldHash(fh);
			return findSeg(seg0addr, addr);
		}
	} else return ret;
}
		
uint8_t SDHashClass::statSeg(SDHAddress addr, SegmentInfo* sinfo) {
	uint8_t meta[kSDHashSegmentMetaSize];
	if (!_card.readData(addr, 0, sizeof meta, meta)) {
		return SDH_ERR_SD;
	}

	if (meta[0] == kSDHashSegment) {
		if (sinfo) {
			memcpy(&sinfo->segment0_addr, meta+1, sizeof sinfo->segment0_addr);
			memcpy(&sinfo->length, meta+1+sizeof sinfo->segment0_addr, sizeof sinfo->length);
			sinfo->length = _BSWAP16(sinfo->length);
		}

		return SDH_OK;
	} else if (meta[0] == kSDHashFreeSegment) return SDH_ERR_FILE_NOT_FOUND;

	return SDH_ERR_WRONG_SEGMENT_TYPE;
}

uint8_t SDHashClass::statSeg0(SDHAddress addr, FileInfo *finfo) {
	// type + hash + seg count
	uint8_t meta[kSDHashSegment0MetaHeaderSize];
	if (!_card.readData(addr, 0, sizeof meta, meta)) {
		return SDH_ERR_SD;
	}

	if (meta[0] == kSDHashSegment0) {
		if (finfo) {
			memcpy(&finfo->hash, meta+1, sizeof finfo->hash);
			memcpy(&finfo->segments_count, meta+1+sizeof finfo->hash, sizeof finfo->segments_count);
			finfo->segments_count = _BSWAP16(finfo->segments_count);
		}

		return SDH_OK;
	} else if (meta[0] == kSDHashFreeSegment) return SDH_ERR_FILE_NOT_FOUND;

	return SDH_ERR_WRONG_SEGMENT_TYPE;
}

uint8_t SDHashClass::statFile(SDHFilehandle fh, FileInfo *finfo, SDHAddress *addrPtr) {
	SDHAddress addr = _foldHash(fh);
	SDHAddress addr0 = addr;

	uint8_t ret;
	FileInfo info;
	do {
		if (addrPtr) *addrPtr = addr;
		ret = statSeg0(addr, &info);
		if (ret == SDH_OK) {
			if (info.hash == fh) {
				if (finfo) *finfo = info;
				return SDH_OK;
			}
		} else if (ret == SDH_ERR_FILE_NOT_FOUND) return ret;

		addr+=STEP(addr0);
	} while (addr != addr0);
	
	return SDH_ERR_NO_SPACE;
}

uint8_t SDHashClass::truncateFile(SDHFilehandle fh, SDHSegmentCount count) {
	FileInfo finfo;
	SDHAddress seg0addr;
	uint8_t ret = statFile(fh, &finfo, &seg0addr);
	if (ret != SDH_OK) return ret;

	if (count > finfo.segments_count) {
		return SDH_ERR_INVALID_ARGUMENT;
	}
	
	while (count) {
		SDHAddress seg_addr;
		finfo.segments_count-=1;
		ret = findSeg(fh, finfo.segments_count, &seg_addr);
		if (ret != SDH_OK) return ret;
		
		uint8_t ret = zero(seg_addr, 1);
		if (ret != SDH_OK) return ret;

		count-=1;
	}
	
	// update segments count
	return _updateSeg0SegmentsCount(seg0addr, finfo.segments_count);
}

/***************************************************************
 * Private Methods
 * *************************************************************/
uint8_t SDHashClass::_updateSeg0SegmentsCount(SDHAddress seg0addr, uint16_t segments_count) {
	// we have to read in the entire metadata b/c we need to preserve 
	// the filename too
	uint8_t meta[kSDHashSegment0MetaSize];
	if (!_card.readData(seg0addr, 0, sizeof meta, meta)) return SDH_ERR_SD;
	segments_count = _BSWAP16(segments_count);
	memcpy(meta+1+sizeof(SDHFilehandle), &segments_count, sizeof segments_count);
	if (!_card.writeBlock(seg0addr, meta, sizeof meta)) return SDH_ERR_SD;
	return SDH_OK;
}

uint8_t SDHashClass::_writeSegment(SDHAddress seg0addr, SDHAddress addr, uint8_t *data, SDHDataSize len) {
	if (!_card.writeStart(addr, 1)) return SDH_ERR_SD;

	uint8_t ofs = 0;
	uint8_t type[1] = {kSDHashSegment};

	if (!_card.writeData(type, sizeof type, ofs)) return SDH_ERR_SD;
	ofs += sizeof type;

	seg0addr = _BSWAP32(seg0addr);
	if (!_card.writeData((uint8_t*)&seg0addr, sizeof seg0addr, ofs)) return SDH_ERR_SD;
	ofs += sizeof seg0addr;

	len = _BSWAP16(len);
	if (!_card.writeData((uint8_t*)&len, sizeof len, ofs)) return SDH_ERR_SD;
	len = _BSWAP16(len);
	ofs += sizeof len;

	if (!_card.writeData(data, len, ofs)) return SDH_ERR_SD;
	// don't add len to ofs, since len is 16 bit while ret is 8

	if (!_card.writeDataPadding(512-ofs-len)) return SDH_ERR_SD;
	if (!_card.writeStop()) return SDH_ERR_SD;

	return SDH_OK;
}

uint8_t SDHashClass::_appendLog(uint8_t type, SDHAddress seg0addr) {
	uint8_t entry[sizeof type + sizeof seg0addr];
	entry[0] = type;
	seg0addr = _BSWAP32(seg0addr);
	memcpy(entry+1, &seg0addr, sizeof seg0addr);
	return appendFile(kSDHashLogFilenameHash, entry, sizeof entry);
}

uint32_t SDHashClass::_incHash(uint32_t hash) {
	return fnv((uint8_t*)&hash, sizeof hash, hash);
}

SDHAddress SDHashClass::_foldHash(uint32_t hash) {
	return 1+hash%(_hashInfo.buckets-1);
}

bool SDHashClass::_getHashInfo() {
	_hashInfo.buckets = 0;
	uint8_t header[kSDHashHeaderSize];

	_card.readData(0, 0, sizeof header, header);

	if (memcmp(header, kSDHashMagic, sizeof kSDHashMagic)) return false;

	_hashInfo.version = header[sizeof kSDHashMagic];
	
	memcpy(&_hashInfo.buckets, header+sizeof kSDHashMagic + 1, sizeof _hashInfo.buckets);

	_hashInfo.buckets = _BSWAP32(_hashInfo.buckets);

	return true;
}

SDHashClass SDHash;
