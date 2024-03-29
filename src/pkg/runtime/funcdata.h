// Copyright 2013 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// This file defines the IDs for PCDATA and FUNCDATA instructions
// in Go binaries. It is included by both C and assembly, so it must
// be written using #defines. It is included by the runtime package
// as well as the compilers.

#define PCDATA_ArgSize 0 /* argument size at CALL instruction */

#define FUNCDATA_GC 0 /* garbage collector block */

// To be used in assembly.
#define ARGSIZE(n) PCDATA $PCDATA_ArgSize, $n

// ArgsSizeUnknown is set in Func.argsize to mark all functions
// whose argument size is unknown (C vararg functions, and
// assembly code without an explicit specification).
// This value is generated by the compiler, assembler, or linker.
#define ArgsSizeUnknown 0x80000000
