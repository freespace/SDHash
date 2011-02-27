Introduction
============

The SD library that comes with Arduinos uses FAT12/16 which means the compiled
code size is relatively large, and not suitable for sketches that require
file-like features while doing non-trivial tasks.

To this end SDHash was created, where we treat the SD cards as a giant
hashtable. That and I thought it might be fun to 'design' a trivial filesystem
and implement it.

Note that if program size is the primary concern, then the tinyFAT library by
Henning Karlsen is smaller for basic operations.  e.g. reading a file using
tinyFAT comes to ~5k while a similar example using SDHash comes to ~7.5k.
However use of tinyFAT requires a 512 byte buffer - half the SRAM available -
while SDHash has no such requirement.

tinyFAT can be found at:

	http://henningkarlsen.com/electronics/a_l_tinyfat.php

Other alternatives include uFAT and SDuFAT available at:

	http://arduinonut.blogspot.com/2008/04/ufat.html
	http://blushingboy.net/p/SDuFAT/page/SDuFAT-basic/

The advantages of these methods is that the card data can easily read on a
desktop, while SDHash by virtue of its ad-hoc nature requires custom programs
and/or FUSE modules.

In theory it is possible to use the *uFAT solutions in combination with SDHash,
but this doesn't really solve the problem of extracting data.

Overview
========

SDHash treats the SD card as a giant hashtable where buckets map to blocks,
and keys map to addresses. Files are stored in buckets as segments. Files are
stored sparsely, meaning each segment is not necessarily 512 bytes long. So
while a file may take up 5 buckets, its size is (probably) < 512 bytes.

Hash Table Keying
=================

Each bucket holds 512 bytes, fixed to the nature of SD/MMC cards. Files are
split into segments and each segment is keyed by repeatly hashing the
filename.

e.g. 

	segkey[0] = hash(filename)
	segkey[1] = hash(segkey[0])
	...
	segkey[n] = hash(segkey[n-1])

Bucket locations are produced by computing FNV-1a (32) hash of the keys and
mod-folding down to the required size:

	http://www.isthe.com/chongo/tech/comp/fnv/
	http://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function

Segment Metadata
================

All segments but the first has the following metadata:

	8 bit segment type:
		0x00 = free segment
		0x01 = first segment, i.e. segment 0
		0x02 = other segments

	32 bit segment 0 address

	16 bit segment length

The first segment stores file metadata:

	8 bit segment type

	32 bit hash

	16 bit segment count, including the first segment.

	24 bytes of filename with maximum filename length of 23 bytes, and thus
	at least one padding byte using PKCS7 padding. This is only really for
	human consumption, and for when the 32bit hash also collides. Therefore
	this should be a human readable string. Originally this was 63 bytes, but
	it took far too much RAM to manipulate.

The reason we put the segment length into the segment metadata and keep track
of the number of segments instead of the number of bytes in the file is to
allow 'easy' appending: we can append to a file simply by incrementing the
segment count in segment 0, and writing new data into a new segment. This can
produce 'sparse' files where each segment is mostly empty. 

Note that the first segment stores no data *at all*. This is because we need
to update it for each new segment appended, and it is difficult to do subblock
modifications of a fullblock with little RAM.


Collisions
==========

Collisions are handled by inc/decrementing the address derived from the hash
until a free bucket is found.  The choice of incrementation vs decrementation
is based on the initial address value:

	if address is odd, increment
	if address is even, decrement

e.g. 

	addr0 = mod-fold hash
	addr += (addr0%2?1:-1)

Deletions
=========

Files are deleted by zero'ing each of its segments, including segment 0.
Additionally deletions are recorded in __LOG if file is not a hidden file.

Hashtable Metadata
==================

Since segment address of 0 is used to signal deletion, segment 0 is't used for
file storage. We exploit this using this segment to allow us to identify a SD
card being used as Aethernet hashtable storage, to identify which version of
hashtable spec is being followed, and the number of buckets in the event
a smaller SD card was bit-for-bit copied to a larger one.

	0xae 'h' 'a' 's' 'h'
	version  number, e.g. 0x01
	16bit table size (number of buckets)


Files Metadata and Hidden Files
===============================

To facilitate discovery of files stored in SDHash, a special file called
'__LS' is used to keep track of filenames. Any time a file is created, its
segment 0 address is appended to __LOG as:

	'c' addrLowByte addrHighByte

Files can be omitted from __LOG if they also begin with 2 underscores. This
conveniently means the same create file function can be used to create __LS.

When files are deleted, they are not removed from __LS, but written to it
again with

	'd' addrLowByte addrHighByte

Mod-Folding
===========

Because segment 0 is 'reserved' the usable number of buckets is reduced by 1.
Thus mod-folding is performed as follows:

	block number = 1 + hash % (number of buckets - 1) 

Endian
======

All integer values are stored little endian, so LSB is the first byte
encountered. This is done to ease interpolation with AVRs.

Implementation Issues
=====================

The library currently uses uint16_t (SDHDataSize) for expressing the amount of
data to read/write to files. This is done to conserve stack space, and since
even the 328 doesn't have 16k of SRAM.
