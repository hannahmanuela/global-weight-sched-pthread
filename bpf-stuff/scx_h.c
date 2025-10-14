/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
* Copyright (c) 2022 Tejun Heo <tj@kernel.org>
* Copyright (c) 2022 David Vernet <dvernet@meta.com>
*/
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_h.bpf.skel.h"

const char help_fmt[] =
"A hierarchical global accounting sched_ext scheduler.\n"
"\n"
"This scheduler implements the global accounting logic from global-accounting-cas.c\n"
"in a BPF sched_ext program. It maintains global virtual time tracking and\n"
"supports cgroup hierarchies with proper weight-based scheduling.\n"
"\n"
"Usage: %s [-v]\n"
"\n"
"  -v            Print libbpf debug messages\n"
"  -h            Display this help and exit\n";

static bool verbose;
static volatile int exit_req;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    if (level == LIBBPF_DEBUG && !verbose)
        return 0;
    return vfprintf(stderr, format, args);
}

static void sigint_handler(int simple)
{
    exit_req = 1;
}

int main(int argc, char **argv)
{
    struct scx_h *skel;
    struct bpf_link *link;
    __u32 opt;
    __u64 ecode;

    libbpf_set_print(libbpf_print_fn);
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
restart:
    skel = SCX_OPS_OPEN(h_ops, scx_h);

    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
        case 'v':
            verbose = true;
            break;
        default:
            fprintf(stderr, help_fmt, basename(argv[0]));
            return opt != 'h';
        }
    }

    SCX_OPS_LOAD(skel, h_ops, scx_h, uei);
    link = SCX_OPS_ATTACH(skel, h_ops, scx_h);

    printf("Hierarchical global accounting scheduler loaded\n");
    printf("Press Ctrl+C to exit\n");

    while (!exit_req && !UEI_EXITED(skel, uei)) {
        sleep(1);
    }

    bpf_link__destroy(link);
    ecode = UEI_REPORT(skel, uei);
    scx_h__destroy(skel);

    if (UEI_ECODE_RESTART(ecode))
        goto restart;
    return 0;
}
