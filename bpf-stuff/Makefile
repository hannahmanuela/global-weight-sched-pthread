

BPF_CLANG ?= clang
BPF_LLVM_STRIP ?= llvm-strip
BPF_CFLAGS ?= -O2 -g -target bpf -Iinclude
# CFLAGS += -I.include

BPFSRC := scx_simple.bpf.c
BPFELF := scx_simple.bpf.o
BPF_SKEL_HDR := scx_simple.bpf.skel.h
USER_SRC := scx_simple.c
USER_BIN := scx_simple

# LIBBPF_OBJ ?= -lbpf
# PKGS ?= libbpf

.PHONY: all clean

all: $(USER_BIN)

$(BPFELF): $(BPFSRC)
	$(BPF_CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(BPF_LLVM_STRIP) -g $@

$(BPF_SKEL_HDR): $(BPFELF)
	bpftool gen skeleton $< name scx_simple > $@

$(USER_BIN): $(USER_SRC) $(BPF_SKEL_HDR)
	$(CC) -g -O2 -Wall -I. -Iinclude -Ilibbpf/include -o $@ $(USER_SRC) -Llibbpf/lib64 -Wl,-rpath,libbpf/lib64 -lbpf

clean:
	rm -f $(BPFELF) $(BPF_SKEL_HDR) $(USER_BIN)

