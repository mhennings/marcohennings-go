// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build darwin freebsd,amd64 freebsd,386 openbsd

#include "runtime.h"
#include "defs_GOOS_GOARCH.h"

// Integrated network poller (kqueue-based implementation).

int32	runtime·kqueue(void);
int32	runtime·kevent(int32, Kevent*, int32, Kevent*, int32, Timespec*);
void	runtime·closeonexec(int32);

static int32 kq = -1;

void
runtime·netpollinit(void)
{
	kq = runtime·kqueue();
	if(kq < 0) {
		runtime·printf("netpollinit: kqueue failed with %d\n", -kq);
		runtime·throw("netpollinit: kqueue failed");
	}
	runtime·closeonexec(kq);
}

int32
runtime·netpollopen(uintptr fd, PollDesc *pd)
{
	Kevent ev[2];
	int32 n;

	// Arm both EVFILT_READ and EVFILT_WRITE in edge-triggered mode (EV_CLEAR)
	// for the whole fd lifetime.  The notifications are automatically unregistered
	// when fd is closed.
	ev[0].ident = (uint32)fd;
	ev[0].filter = EVFILT_READ;
	ev[0].flags = EV_ADD|EV_CLEAR;
	ev[0].fflags = 0;
	ev[0].data = 0;
	ev[0].udata = (byte*)pd;
	ev[1] = ev[0];
	ev[1].filter = EVFILT_WRITE;
	n = runtime·kevent(kq, ev, 2, nil, 0, nil);
	if(n < 0)
		return -n;
	return 0;
}

int32
runtime·netpollclose(uintptr fd)
{
	// Don't need to unregister because calling close()
	// on fd will remove any kevents that reference the descriptor.
	USED(fd);
	return 0;
}

// Polls for ready network connections.
// Returns list of goroutines that become runnable.
G*
runtime·netpoll(bool block)
{
	static int32 lasterr;
	Kevent events[64], *ev;
	Timespec ts, *tp;
	int32 n, i, mode;
	G *gp;

	if(kq == -1)
		return nil;
	tp = nil;
	if(!block) {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		tp = &ts;
	}
	gp = nil;
retry:
	n = runtime·kevent(kq, nil, 0, events, nelem(events), tp);
	if(n < 0) {
		if(n != -EINTR && n != lasterr) {
			lasterr = n;
			runtime·printf("runtime: kevent on fd %d failed with %d\n", kq, -n);
		}
		goto retry;
	}
	for(i = 0; i < n; i++) {
		ev = &events[i];
		mode = 0;
		if(ev->filter == EVFILT_READ)
			mode += 'r';
		if(ev->filter == EVFILT_WRITE)
			mode += 'w';
		if(mode)
			runtime·netpollready(&gp, (PollDesc*)ev->udata, mode);
	}
	if(block && gp == nil)
		goto retry;
	return gp;
}
