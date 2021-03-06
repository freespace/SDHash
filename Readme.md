Introduction
============

The SD library that comes with Arduinos uses FAT12/16 which means the compiled
code size is relatively large, and not suitable for sketches that require
file-like features while doing non-trivial tasks.

To this end SDHash was created, where we treat the SD cards as a giant
hashtable. That and I thought it might be fun to 'design' a trivial filesystem
and implement it.

Comparison to FAT Libraries
===========================

In comparison to FAT based libraries, SDHash consistently produces smaller
programs and uses less RAM. A simple create-write-read-delete program with a
126 character string as payload compiles to 7.8K. tinyFAT, the smallest FAT
library I can find that offers the same functions does the same thing in 10.3K.

tinyFAT can be found at:

	http://henningkarlsen.com/electronics/a_l_tinyfat.php

Other alternatives (with more limitations) include uFAT and SDuFAT available
at:

	http://arduinonut.blogspot.com/2008/04/ufat.html
	http://blushingboy.net/p/SDuFAT/page/SDuFAT-basic/

An advantage of the FAT approach is interoperability with desktop computers.
SDHash will likely gain a FUSE module in the future, but until then getting the
data off the SD card is an excercise left to the reader. As such I do recommend
trying the FAT approaches before using SDHash.

Note that the above FAT libraries all require 512 byte buffer for write
operations. SDHash has no such requirement because it can generate padding 0x0
bytes.

Credit
======

Many thanks to the developers of sdfatlib:

	http://code.google.com/p/sdfatlib/

Their excellent Sd2Card library was instrumental in developing SDHash quickly.
I have modified it to allow for writing blocks using less than 512 bytes of
data, with the difference being generated and provided to the SD card.

How it Works
============

SDHash treats the SD card as a giant hashtable where buckets map to blocks,
and keys map to addresses. Files are stored in buckets as segments. Files are
stored sparsely, meaning each segment is not necessarily 512 bytes long. So
while a file may take up 5 buckets, its size is probably smaller than 5x512 
bytes.

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

The implementation used sets hval to the 32bit offset bias whenever hval has a
value of zero. This is to avoid the issue noted where otherwise a buffer of all
0s hashes to a value of 0.

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
modifications of a full block with little RAM.

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
Additionally deletions are recorded in `__LOG` if file is not a hidden file.

Hashtable Metadata
==================

The first block on the SD card is reserved for hashtable metadata and magic,
which allow us to identify a SDHash SD card and to identify which version of
hashtable spec is being followed. Additionally the number of buckets is also
stored in the event a smaller SD card was bit-for-bit copied to a larger one.

	0xae 'h' 'a' 's' 'h'
	version  number, e.g. 0x01
	32bit table size (number of buckets)


Files Metadata and Hidden Files
===============================

To facilitate discovery of files stored in SDHash, a special file called
`__LOG` is used to keep track of filenames. Any time a file is created, its
segment 0 address is appended to `__LOG` as:

	addressbytes 'c'

Files can be omitted from `__LOG` if they also begin with 2 underscores. This
conveniently means the same create file function can be used to create `__LOG`.
By placing the type byte last, we can detect incomplete log entries by
requiring all log entries to end in 'c'.

When files are deleted, they are not removed from `__LOG`, but written to it
again with

	addressbytes 'd'

This is done to aid faster creation/deletion. This might change in the future. 

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

The library currently uses `uint16_t` (SDHDataSize) for expressing the amount of
data to read/write to files. This is done to conserve stack space, and since
even the 328 doesn't have 16k of SRAM.

Furthermore, though the specification above calls for using the 23 character
filename to do comparisons when the 32bit hash collides, it isn't actually done
at the moment.

