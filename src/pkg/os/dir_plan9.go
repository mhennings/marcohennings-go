// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package os

import (
	"syscall"
)

type dirInfo int

var markDirectory dirInfo = ^0

// Readdir reads the contents of the directory associated with file and
// returns an array of up to count FileInfo structures, as would be returned
// by Lstat, in directory order.  Subsequent calls on the same file will yield
// further FileInfos. A negative count means to read the entire directory.
// Readdir returns the array and an Error, if any.
func (file *File) Readdir(count int) (fi []FileInfo, err Error) {
	// If this file has no dirinfo, create one.
	if file.dirinfo == nil {
		file.dirinfo = &markDirectory
	}

	size := count
	if size < 0 {
		size = 100
	}

	result := make([]FileInfo, 0, size)
	var buf [syscall.STATMAX]byte

	for {
		n, e := file.Read(buf[:])

		if e != nil {
			if e == EOF {
				break
			}

			return []FileInfo{}, &PathError{"readdir", file.name, e}
		}

		if n < syscall.STATFIXLEN {
			return []FileInfo{}, &PathError{"readdir", file.name, Eshortstat}
		}

		for i := 0; i < n; {
			m, _ := gbit16(buf[i:])
			m += 2

			if m < syscall.STATFIXLEN {
				return []FileInfo{}, &PathError{"readdir", file.name, Eshortstat}
			}

			d, e := UnmarshalDir(buf[i : i+int(m)])

			if e != nil {
				return []FileInfo{}, &PathError{"readdir", file.name, e}
			}

			var f FileInfo
			fileInfoFromStat(&f, d)

			result = append(result, f)

			// a negative count means to read until EOF.
			if count > 0 && len(result) >= count {
				break
			}

			i += int(m)
		}
	}

	return result, nil
}

// Readdirnames returns an array of up to count file names residing in the 
// directory associated with file. A negative count will return all of them.
// Readdir returns the array and an Error, if any.
func (file *File) Readdirnames(count int) (names []string, err Error) {
	fi, e := file.Readdir(count)

	if e != nil {
		return []string{}, e
	}

	names = make([]string, len(fi))
	err = nil

	for i, _ := range fi {
		names[i] = fi[i].Name
	}

	return
}

type Dir struct {
	// system-modified data
	Type uint16 // server type
	Dev  uint32 // server subtype
	// file data
	Qid    Qid    // unique id from server
	Mode   uint32 // permissions
	Atime  uint32 // last read time
	Mtime  uint32 // last write time
	Length uint64 // file length
	Name   string // last element of path
	Uid    string // owner name
	Gid    string // group name
	Muid   string // last modifier name
}

type Qid struct {
	Path uint64 // the file server's unique identification for the file
	Vers uint32 // version number for given Path
	Type uint8  // the type of the file (syscall.QTDIR for example)
}

var nullDir = Dir{
	^uint16(0),
	^uint32(0),
	Qid{^uint64(0), ^uint32(0), ^uint8(0)},
	^uint32(0),
	^uint32(0),
	^uint32(0),
	^uint64(0),
	"",
	"",
	"",
	"",
}

// Null assigns members of d with special "don't care" values indicating
// they should not be written by syscall.Wstat. 
func (d *Dir) Null() {
	*d = nullDir
}

// pdir appends a 9P Stat message based on the contents of Dir d to a byte slice b.
func pdir(b []byte, d *Dir) []byte {
	n := len(b)
	b = pbit16(b, 0) // length, filled in later	
	b = pbit16(b, d.Type)
	b = pbit32(b, d.Dev)
	b = pqid(b, d.Qid)
	b = pbit32(b, d.Mode)
	b = pbit32(b, d.Atime)
	b = pbit32(b, d.Mtime)
	b = pbit64(b, d.Length)
	b = pstring(b, d.Name)
	b = pstring(b, d.Uid)
	b = pstring(b, d.Gid)
	b = pstring(b, d.Muid)
	pbit16(b[0:n], uint16(len(b)-(n+2)))
	return b
}

// UnmarshalDir reads a 9P Stat message from a 9P protocol message strored in b,
// returning the corresponding Dir struct.
func UnmarshalDir(b []byte) (d *Dir, err Error) {
	n := uint16(0)
	n, b = gbit16(b)

	if int(n) != len(b) {
		return nil, Ebadstat
	}

	d = new(Dir)
	d.Type, b = gbit16(b)
	d.Dev, b = gbit32(b)
	d.Qid, b = gqid(b)
	d.Mode, b = gbit32(b)
	d.Atime, b = gbit32(b)
	d.Mtime, b = gbit32(b)
	d.Length, b = gbit64(b)
	d.Name, b = gstring(b)
	d.Uid, b = gstring(b)
	d.Gid, b = gstring(b)
	d.Muid, b = gstring(b)

	if len(b) != 0 {
		return nil, Ebadstat
	}

	return d, nil
}

// gqid reads the qid part of a 9P Stat message from a 9P protocol message strored in b,
// returning the corresponding Qid struct and the remaining slice of b.
func gqid(b []byte) (Qid, []byte) {
	var q Qid
	q.Path, b = gbit64(b)
	q.Vers, b = gbit32(b)
	q.Type, b = gbit8(b)
	return q, b
}

// pqid appends a Qid struct q to a 9P message b.
func pqid(b []byte, q Qid) []byte {
	b = pbit64(b, q.Path)
	b = pbit32(b, q.Vers)
	b = pbit8(b, q.Type)
	return b
}

// gbit8 reads a byte-sized numeric value from a 9P protocol message strored in b,
// returning the value and the remaining slice of b.
func gbit8(b []byte) (uint8, []byte) {
	return uint8(b[0]), b[1:]
}

// gbit16 reads a 16-bit numeric value from a 9P protocol message strored in b,
// returning the value and the remaining slice of b.
func gbit16(b []byte) (uint16, []byte) {
	return uint16(b[0]) | uint16(b[1])<<8, b[2:]
}

// gbit32 reads a 32-bit numeric value from a 9P protocol message strored in b,
// returning the value and the remaining slice of b.
func gbit32(b []byte) (uint32, []byte) {
	return uint32(b[0]) | uint32(b[1])<<8 | uint32(b[2])<<16 | uint32(b[3])<<24, b[4:]
}

// gbit64 reads a 64-bit numeric value from a 9P protocol message strored in b,
// returning the value and the remaining slice of b.
func gbit64(b []byte) (uint64, []byte) {
	lo, b := gbit32(b)
	hi, b := gbit32(b)
	return uint64(hi)<<32 | uint64(lo), b
}

// gstring reads a string from a 9P protocol message strored in b,
// returning the value as a Go string and the remaining slice of b.
func gstring(b []byte) (string, []byte) {
	n, b := gbit16(b)
	return string(b[0:n]), b[n:]
}

// pbit8 appends a byte-sized numeric value x to a 9P message b.
func pbit8(b []byte, x uint8) []byte {
	n := len(b)
	if n+1 > cap(b) {
		nb := make([]byte, n, 100+2*cap(b))
		copy(nb, b)
		b = nb
	}
	b = b[0 : n+1]
	b[n] = x
	return b
}

// pbit16 appends a 16-bit numeric value x to a 9P message b.
func pbit16(b []byte, x uint16) []byte {
	n := len(b)
	if n+2 > cap(b) {
		nb := make([]byte, n, 100+2*cap(b))
		copy(nb, b)
		b = nb
	}
	b = b[0 : n+2]
	b[n] = byte(x)
	b[n+1] = byte(x >> 8)
	return b
}

// pbit32 appends a 32-bit numeric value x to a 9P message b.
func pbit32(b []byte, x uint32) []byte {
	n := len(b)
	if n+4 > cap(b) {
		nb := make([]byte, n, 100+2*cap(b))
		copy(nb, b)
		b = nb
	}
	b = b[0 : n+4]
	b[n] = byte(x)
	b[n+1] = byte(x >> 8)
	b[n+2] = byte(x >> 16)
	b[n+3] = byte(x >> 24)
	return b
}

// pbit64 appends a 64-bit numeric value x to a 9P message b.
func pbit64(b []byte, x uint64) []byte {
	b = pbit32(b, uint32(x))
	b = pbit32(b, uint32(x>>32))
	return b
}

// pstring appends a Go string s to a 9P message b.
func pstring(b []byte, s string) []byte {
	if len(s) >= 1<<16 {
		panic(NewError("string too long"))
	}
	b = pbit16(b, uint16(len(s)))
	b = append(b, []byte(s)...)
	return b
}
