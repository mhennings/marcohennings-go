// Copyright 2009 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package net

import "syscall"

func maxListenerBacklog() int {
	// TODO: Implement this
	// NOTE: Never return a number bigger than 1<<16 - 1. See issue 5030.
	return syscall.SOMAXCONN
}

func listenerSockaddr(s syscall.Handle, f int, laddr sockaddr) (syscall.Sockaddr, error) {
	switch laddr := laddr.(type) {
	case *TCPAddr, *UnixAddr:
		if err := setDefaultListenerSockopts(s); err != nil {
			return nil, err
		}
		return laddr.sockaddr(f)
	case *UDPAddr:
		if laddr.IP != nil && laddr.IP.IsMulticast() {
			if err := setDefaultMulticastSockopts(s); err != nil {
				return nil, err
			}
			addr := *laddr
			switch f {
			case syscall.AF_INET:
				addr.IP = IPv4zero
			case syscall.AF_INET6:
				addr.IP = IPv6unspecified
			}
			laddr = &addr
		}
		return laddr.sockaddr(f)
	default:
		return laddr.sockaddr(f)
	}
}

func sysSocket(f, t, p int) (syscall.Handle, error) {
	// See ../syscall/exec_unix.go for description of ForkLock.
	syscall.ForkLock.RLock()
	s, err := syscall.Socket(f, t, p)
	if err == nil {
		syscall.CloseOnExec(s)
	}
	syscall.ForkLock.RUnlock()
	return s, err
}
