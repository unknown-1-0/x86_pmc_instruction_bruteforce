.global enter_user
// rcx - rip
// rdx - rsp
enter_user:
push 0x23 // ss
push rdx // rsp
push (1<<8) | 2 // rflags.tf = 1
push 0x1b // cs
push rcx // rip

mov rax, [rip+xsave_state_area_ptr]
xrstor [rax]

xor eax, eax
xor ecx, ecx
xor edx, edx
xor ebx, ebx
xor ebp, ebp
xor esi, esi
xor edi, edi
xor r8d, r8d
xor r9d, r9d
xor r10d, r10d
xor r11d, r11d
xor r12d, r12d
xor r13d, r13d
xor r14d, r14d
xor r15d, r15d
finit
iretq

