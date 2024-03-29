// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build freebsd netbsd openbsd

package syscall

const ImplementsGetwd = false

func Getwd() (string, error) { return "", ENOTSUP }
