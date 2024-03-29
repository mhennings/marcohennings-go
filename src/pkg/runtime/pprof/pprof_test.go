// Copyright 2011 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// See issue 5659.
// +build !race

package pprof_test

import (
	"bytes"
	"hash/crc32"
	"os/exec"
	"runtime"
	. "runtime/pprof"
	"strings"
	"testing"
	"unsafe"
)

func TestCPUProfile(t *testing.T) {
	buf := make([]byte, 100000)
	testCPUProfile(t, []string{"crc32.ChecksumIEEE"}, func() {
		// This loop takes about a quarter second on a 2 GHz laptop.
		// We only need to get one 100 Hz clock tick, so we've got
		// a 25x safety buffer.
		for i := 0; i < 1000; i++ {
			crc32.ChecksumIEEE(buf)
		}
	})
}

func TestCPUProfileMultithreaded(t *testing.T) {
	buf := make([]byte, 100000)
	defer runtime.GOMAXPROCS(runtime.GOMAXPROCS(2))
	testCPUProfile(t, []string{"crc32.ChecksumIEEE", "crc32.Update"}, func() {
		c := make(chan int)
		go func() {
			for i := 0; i < 2000; i++ {
				crc32.Update(0, crc32.IEEETable, buf)
			}
			c <- 1
		}()
		// This loop takes about a quarter second on a 2 GHz laptop.
		// We only need to get one 100 Hz clock tick, so we've got
		// a 25x safety buffer.
		for i := 0; i < 2000; i++ {
			crc32.ChecksumIEEE(buf)
		}
	})
}

func testCPUProfile(t *testing.T, need []string, f func()) {
	switch runtime.GOOS {
	case "darwin":
		out, err := exec.Command("uname", "-a").CombinedOutput()
		if err != nil {
			t.Fatal(err)
		}
		vers := string(out)
		t.Logf("uname -a: %v", vers)
	case "plan9":
		// unimplemented
		return
	}

	var prof bytes.Buffer
	if err := StartCPUProfile(&prof); err != nil {
		t.Fatal(err)
	}
	f()
	StopCPUProfile()

	// Convert []byte to []uintptr.
	bytes := prof.Bytes()
	l := len(bytes) / int(unsafe.Sizeof(uintptr(0)))
	val := *(*[]uintptr)(unsafe.Pointer(&bytes))
	val = val[:l]

	if l < 13 {
		if runtime.GOOS == "darwin" {
			t.Logf("ignoring failure on OS X; see golang.org/issue/6047")
			return
		}
		t.Fatalf("profile too short: %#x", val)
	}

	hd, val, tl := val[:5], val[5:l-3], val[l-3:]
	if hd[0] != 0 || hd[1] != 3 || hd[2] != 0 || hd[3] != 1e6/100 || hd[4] != 0 {
		t.Fatalf("unexpected header %#x", hd)
	}

	if tl[0] != 0 || tl[1] != 1 || tl[2] != 0 {
		t.Fatalf("malformed end-of-data marker %#x", tl)
	}

	// Check that profile is well formed and contains ChecksumIEEE.
	have := make([]uintptr, len(need))
	for len(val) > 0 {
		if len(val) < 2 || val[0] < 1 || val[1] < 1 || uintptr(len(val)) < 2+val[1] {
			t.Fatalf("malformed profile.  leftover: %#x", val)
		}
		for _, pc := range val[2 : 2+val[1]] {
			f := runtime.FuncForPC(pc)
			if f == nil {
				continue
			}
			for i, name := range need {
				if strings.Contains(f.Name(), name) {
					have[i] += val[0]
				}
			}
		}
		val = val[2+val[1]:]
	}

	var total uintptr
	for i, name := range need {
		total += have[i]
		t.Logf("%s: %d\n", name, have[i])
	}
	ok := true
	if total == 0 {
		t.Logf("no CPU profile samples collected")
		ok = false
	}
	min := total / uintptr(len(have)) / 2
	for i, name := range need {
		if have[i] < min {
			t.Logf("%s has %d samples out of %d, want at least %d, ideally %d", name, have[i], total, min, total/uintptr(len(have)))
			ok = false
		}
	}

	if !ok {
		if runtime.GOOS == "darwin" {
			t.Logf("ignoring failure on OS X; see golang.org/issue/6047")
			return
		}
		t.FailNow()
	}
}
