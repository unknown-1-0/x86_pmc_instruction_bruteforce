.global load_segments

// rcx - code segment
// rdx - stack segment
// r8 - task segment
load_segments:
lea rax, [rip+.L1]
push rcx
push rax
retfq
.L1:
mov es, rdx
mov ss, rdx
mov ds, rdx
mov fs, rdx
mov gs, rdx
ltr r8w
ret

