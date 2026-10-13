#pragma once
#define F_SETFL 0
static inline int fcntl(int fd, int cmd, int arg) { (void)fd; (void)cmd; (void)arg; return 0; }
