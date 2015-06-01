/*
 * Copyright (C) 2015 TOSHIBA corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Usage: ./seccomp-ids-wrapper [options] config -- app <args>
 */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/audit.h>
#include <syscall.h>
#include <sys/socket.h>
#include <time.h>

#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <asm/unistd.h>

# define REG_SYSCALL	REG_EAX
# define ARCH_NR	AUDIT_ARCH_I386

#define SECCOMP_MODE_IDS 3

#define NR_syscalls 350
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define DECLARE_BITMAP(name,bits) unsigned long name[BITS_TO_LONGS(bits)]
#define BITS_PER_BYTE           8
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define BITS_PER_LONG __WORDSIZE
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)

static inline void set_bit(int nr, volatile unsigned long *addr) {
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	*p  |= mask;
}

struct seccomp_idsentry {
	uint32_t syscall_nr;
	DECLARE_BITMAP(next_syscalls, NR_syscalls);
};

struct seccomp_idstable {
	uint32_t len;
	struct seccomp_idsentry *entries;
};

//#define NUM_SYSCALLS 22
#include "./num_syscalls"

static int install_syscall_filter(void)
{
	int i;
	struct seccomp_idstable idstable;

	idstable.len = NUM_SYSCALLS+2;
	idstable.entries = (struct seccomp_idsentry *)
		calloc(NUM_SYSCALLS, sizeof(struct seccomp_idsentry));

#include "./seccomp_settings"

	// very tricky and stupid.. just for a proof-of-concept is fine
	idstable.entries[NUM_SYSCALLS].syscall_nr = 59; // execv in amd64
	idstable.entries[NUM_SYSCALLS+1].syscall_nr = 158; // prctl in amd64
	for (i=0; i<NR_syscalls; i++) {
		set_bit(i, idstable.entries[NUM_SYSCALLS].next_syscalls);
		set_bit(i, idstable.entries[NUM_SYSCALLS+1].next_syscalls);
		set_bit(i, idstable.entries[7].next_syscalls);
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		perror("prctl(NO_NEW_PRIVS)");
		goto failed;
	}

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_IDS, &idstable)) {
		perror("prctl(SECCOMP)");
		goto failed;
	}
	return 0;

failed:
	if (errno == EINVAL)
		fprintf(stderr, "SECCOMP_FILTER is not available. :(\n");
	return 1;
}

int main(int argc, char *argv[])
{
	int ret;
	install_syscall_filter();
	
	// Usage: ./seccomp-ids-wrapper [options] config -- app <args>
	ret = execl(argv[1], argv[1], (char *)NULL);

	return 0;
}

