public trap_arrays

;
;	size_t fastcall my_trace_routine(void*)
;	return value - reserved
;
extern my_trace_routine:proc
extern watch_privilege_process_id:dword
extern watch_privilege_thread_id:dword
extern lp_flag_page_for_asm:qword
extern lp_address_page_for_asm:qword
extern lp_count_page_for_asm:qword

MASK_NeedTrace EQU 2

.code

Generate_Entrys MACRO _routine
	I = 0
	WHILE I LT 4096
		mov word ptr [rsp - 8], I
		jmp _routine
		I = I + 1
	ENDM
ENDM


dispatch_routine proc FRAME

;
;	void __fastcall dispatch_routine()
;
;		we don't care about what's on the stack now,
;	just keep the stack ptr balanced 
;
	align 16

;
;	now we really starts
;	regs used: rax, rbx, rsi
;
	sub rsp, 16			; for id and func-addr
	pushfq
	sub rsp, 128
	mov [rsp], rax
	mov [rsp + 8], rbx
	mov [rsp + 32], rsi
.allocstack 152

;
;	get id and set address
;
	xor rbx, rbx
	mov bx, word ptr [rsp + 144]
	mov rax, lp_address_page_for_asm
	mov rax, qword ptr [rax + rbx * 8]
	mov [rsp + 136], rax

;
;	check for watch privilege
;
	mov rsi, gs:[30h]
	mov eax, dword ptr [rsi + 40h]
	cmp eax, watch_privilege_process_id
	jz skip_all
	mov eax, dword ptr [rsi + 48h]
	cmp eax, watch_privilege_thread_id
	jz skip_all

;
;	MOST of the hooks don't need to trace
;
	mov rax, lp_flag_page_for_asm
	mov al, byte ptr [rax + rbx]
	and al, MASK_NeedTrace
	test al, al
	jz skip_trace

;
;	OK, time to trace
;	We need to save all regs
;
	mov [rsp + 16], rcx
	mov [rsp + 24], rdx
	mov [rsp + 40], rdi
	mov rax, rsp
	sub rax, 152
	mov [rsp + 48], rax
	mov [rsp + 56], rbp
	mov [rsp + 64], r8
	mov [rsp + 72], r9
	mov [rsp + 80], r10
	mov [rsp + 88], r11
	mov [rsp + 96], r12
	mov [rsp + 104], r13
	mov [rsp + 112], r14
	mov [rsp + 120], r15

;
;	aquire watch privilege
;		
	push [rsi + 40h]
	mov eax, watch_privilege_process_id
	mov dword ptr [rsi + 40h], eax
.allocstack 8

;
;	call trace routine as __fastcall
;	need align to 16
;
	lea rcx, [rsp + 8]
	sub rsp, 40
.allocstack 40
.endprolog
	call my_trace_routine
	add rsp, 40

;
;	release watch privilege
;
	pop rax
	mov dword ptr [rsi + 40h], eax

;
;	rstor what we don't need
;
	mov rcx, [rsp + 16]
	mov rdx, [rsp + 24]
	mov rdi, [rsp + 40]
	mov rbp, [rsp + 56]
	mov r8, [rsp + 64]
	mov r9, [rsp + 72]
	mov r10, [rsp + 80]
	mov r11, [rsp + 88]
	mov r12, [rsp + 96]
	mov r13, [rsp + 104]
	mov r14, [rsp + 112]
	mov r15, [rsp + 120]

;
; time to count
;
skip_trace:
	mov rax, lp_count_page_for_asm
	inc dword ptr [rax + rbx * 4]

skip_all:
	mov rax, [rsp]
	mov rbx, [rsp + 8]
	mov rsi, [rsp + 32]
	add rsp, 128
	popfq
	add rsp, 16
	jmp qword ptr [rsp - 16]

dispatch_routine endp


trap_arrays proc

	Generate_Entrys dispatch_routine

trap_arrays endp

end