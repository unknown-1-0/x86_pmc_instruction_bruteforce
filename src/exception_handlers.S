do_handle_exception:
#ifdef SAVE_REGS_ON_EXCEPTION
push r15
push r14
push r13
push r12
push r11
push r10
push r9
push r8
push rdi
push rsi
push rbp
//push rsp
push rbx
push rdx
push rcx
push rax
#endif
mov rcx, rsp
push 0
popfq
jmp handle_exception

.global handle_divide_error
handle_divide_error:
push 0
jmp do_handle_exception

.global handle_debug_exception
handle_debug_exception:
push 1
jmp do_handle_exception

.global handle_nmi
handle_nmi:
push 2
jmp do_handle_exception

.global handle_breakpoint
handle_breakpoint:
push 3
jmp do_handle_exception

.global handle_overflow
handle_overflow:
push 4
jmp do_handle_exception

.global handle_bound_range_exceeded
handle_bound_range_exceeded:
push 5
jmp do_handle_exception

.global handle_invalid_opcode
handle_invalid_opcode:
push 6
jmp do_handle_exception

.global handle_device_not_available
handle_device_not_available:
push 7
jmp do_handle_exception

.global handle_double_fault
handle_double_fault:
push 8
jmp do_handle_exception

.global handle_reserved9
handle_reserved9:
push 9
jmp do_handle_exception

.global handle_invalid_tss
handle_invalid_tss:
push 10
jmp do_handle_exception

.global handle_segment_not_present
handle_segment_not_present:
push 11
jmp do_handle_exception

.global handle_stack_fault
handle_stack_fault:
push 12
jmp do_handle_exception

.global handle_general_protection_fault
handle_general_protection_fault:
push 13
jmp do_handle_exception

.global handle_page_fault
handle_page_fault:
push 14
jmp do_handle_exception

.global handle_reserved15
handle_reserved15:
push 15
jmp do_handle_exception

.global handle_x87_fpu_fp_error
handle_x87_fpu_fp_error:
push 16
jmp do_handle_exception

.global handle_alignment_check
handle_alignment_check:
push 17
jmp do_handle_exception

.global handle_machine_check
handle_machine_check:
push 18
jmp do_handle_exception

.global handle_simd_fp_exception
handle_simd_fp_exception:
push 19
jmp do_handle_exception

.global handle_virtualization_exception
handle_virtualization_exception:
push 20
jmp do_handle_exception

.global handle_control_protection_exception
handle_control_protection_exception:
push 21
jmp do_handle_exception

