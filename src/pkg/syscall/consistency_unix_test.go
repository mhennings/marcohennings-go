// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build freebsd darwin linux netbsd openbsd

// This file tests that some basic syscalls are consistent across
// all Unixes.

package syscall_test

import "syscall"

// {Set,Get}priority and needed constants for them
func _() {
	var (
		_ func(int, int, int) error   = syscall.Setpriority
		_ func(int, int) (int, error) = syscall.Getpriority
	)
	const (
		_ int = syscall.PRIO_USER
		_ int = syscall.PRIO_PROCESS
		_ int = syscall.PRIO_PGRP
	)
}
