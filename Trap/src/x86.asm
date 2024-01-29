.model flat

public _trap_arrays

;
;	size_t __cdelc my_trace_routine(void*)
;	return value - reserved
;
extern _my_trace_routine:proc
extern _watch_privilege_process_id:dword
extern _watch_privilege_thread_id:dword
extern _lp_flag_page_for_asm:dword
extern _lp_address_page_for_asm:dword
extern _lp_count_page_for_asm:dword

MASK_NeedTrace EQU 2

.code

Generate_Entrys MACRO _routine
	I = 0
	WHILE I LT 4096
		mov word ptr [esp - 8], I
		jmp _routine
		I = I + 1
	ENDM
ENDM

dispatch_routine proc

;
;	void __cdelc dispatch_routine()
;
;	don't care about what's on the stack
;
	align 16
	ASSUME FS:NOTHING

;
;	generate stack frame
;
	push ebp
	mov ebp, esp
	sub esp, 8
	pushfd
	sub esp, 32

;
;	save hot registers
;
	mov [ebp - 44], eax
	mov [ebp - 40], ebx
	mov [ebp - 28], esi

;
;	get id and set function address
;
	xor ebx, ebx
	mov bx, word ptr [ebp - 4]
	mov eax, _lp_address_page_for_asm
	mov eax, [eax + ebx * 4]
	mov [ebp - 8], eax

;
;	check for watch privilege
;
	mov esi, fs:[18h]
	mov eax, [esi + 20h]
	cmp eax, _watch_privilege_process_id
	jz skip_all
	mov eax, [esi + 24h]
	cmp eax, _watch_privilege_thread_id
	jz skip_all

;
;	do we need trace ?
;
privilege_not_found:
	mov eax, _lp_flag_page_for_asm
	mov al, byte ptr [eax + ebx]
	and al, MASK_NeedTrace
	test al, al
	jz skip_trace

;
;	time to trace, save regs left
;
	mov [ebp - 36], ecx
	mov [ebp - 32], edx
	mov [ebp - 24], edi
	mov eax, esp
	sub eax, 44
	mov [ebp - 20], eax
	mov eax, [ebp]
	mov [ebp - 16], eax

;
;	acquire watch privilege
;
	push dword ptr [esi + 20h]
	mov eax, _watch_privilege_process_id
	mov [esi + 20h], eax

;
;	call the routine as __cdelc
;	Remember to fix the lpTrapFrame !
;
	lea eax, [esp + 4]
	push eax
	call _my_trace_routine
	add esp, 4

;
;	release watch privilege
;
	pop eax
	mov [esi + 20h], eax

;
;	rstor regs left
;
	mov ecx, [ebp - 36]
	mov edx, [ebp - 32]
	mov edi, [ebp - 24]

;
;	time to count
;
skip_trace:
	mov eax, _lp_count_page_for_asm
	inc dword ptr [eax + ebx * 4]

skip_all:
	mov eax, [ebp - 44]
	mov ebx, [ebp - 40]
	mov esi, [ebp - 28]
	add esp, 32
	popfd
	add esp, 8
	pop ebp
	jmp dword ptr [esp - 12]

dispatch_routine endp


_trap_arrays proc

	Generate_Entrys dispatch_routine

_trap_arrays endp


end