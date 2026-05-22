; PatchWork x64 MASM Assembly
; Replaces inline __asm blocks from CodeFunctions.cpp for x64 compatibility
;
; x64 Windows calling convention:
;   - First 4 integer args: RCX, RDX, R8, R9
;   - Additional args on stack (right to left, 8-byte aligned)
;   - Caller allocates 32-byte shadow space
;   - Return value in RAX
;   - Volatile: RAX, RCX, RDX, R8, R9, R10, R11
;   - Non-volatile: RBX, RBP, RSI, RDI, R12-R15, XMM6-XMM15
;
; Since x64 has only ONE calling convention, the old x86 cdecl/thiscall/stdcall
; dispatch is greatly simplified. All functions use Microsoft x64 ABI.

.code

; External C++ functions we call from assembly
EXTERN SetLuaHookedFunctionParameters : PROC
EXTERN executeLuaHook : PROC
EXTERN GetDetourLuaTargetAndCallTheLuaFunction : PROC

; External C++ variables we reference
EXTERN luaHookedFunctionArgCount : DWORD
EXTERN currentDetourReturn : QWORD

; =============================================================================
; CallMachineCode_x64
; =============================================================================
; Called from C++ luaCallMachineCode() after it has prepared:
;   RCX = target function address
;   RDX = pointer to argument array (DWORD_PTR[])
;   R8  = argument count
;
; This function dynamically sets up the x64 calling convention:
;   - Args 0-3 go into RCX, RDX, R8, R9
;   - Args 4+ go on the stack
;   - 32-byte shadow space is always allocated
;   - Returns the result in RAX
;
CallMachineCode_x64 PROC
    push rbp
    mov rbp, rsp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15

    ; Save parameters
    mov r12, rcx            ; r12 = target function address
    mov r13, rdx            ; r13 = arg array pointer
    mov r14, r8             ; r14 = arg count

    ; Calculate stack space needed:
    ;   - 32 bytes shadow space (always)
    ;   - 8 bytes per arg beyond 4 (if any)
    ;   - Align to 16 bytes
    mov rax, r14
    cmp rax, 4
    jle @stack_args_done
    sub rax, 4              ; extra args beyond 4
    jmp @calc_stack
@stack_args_done:
    xor rax, rax
@calc_stack:
    ; Total stack = 32 (shadow) + rax*8 (extra args)
    lea rax, [rax*8 + 32]
    ; Align to 16 bytes
    add rax, 15
    and rax, 0FFFFFFFFFFFFFFF0h
    sub rsp, rax
    mov r15, rax            ; save stack adjustment

    ; Push extra args (5th, 6th, ...) onto stack
    cmp r14, 5
    jl @no_extra_args
    mov rcx, r14
    sub rcx, 4              ; number of extra args
    lea rsi, [r13 + 4*8]    ; point to 5th arg (index 4)
@push_extra:
    dec rcx
    mov rax, [rsi + rcx*8]
    mov [rsp + 32 + rcx*8], rax
    test rcx, rcx
    jnz @push_extra

@no_extra_args:
    ; Load first 4 args into registers
    cmp r14, 1
    jl @do_call
    mov rcx, [r13]          ; arg 0 -> RCX

    cmp r14, 2
    jl @do_call
    mov rdx, [r13 + 8]      ; arg 1 -> RDX

    cmp r14, 3
    jl @do_call
    mov r8, [r13 + 16]      ; arg 2 -> R8

    cmp r14, 4
    jl @do_call
    mov r9, [r13 + 24]      ; arg 3 -> R9

@do_call:
    ; If no args, still need to clear RCX for safety
    cmp r14, 0
    jne @call_target
    xor ecx, ecx

@call_target:
    call r12                ; call the target function

    ; Restore stack
    add rsp, r15

    ; Return value is already in RAX
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
CallMachineCode_x64 ENDP


; =============================================================================
; LuaLandingFromCpp_x64
; =============================================================================
; This is the naked trampoline that replaces the hooked function's first bytes.
; When a hooked function is called, the CALL instruction redirects here.
;
; x64 version:
;   - The return address from the CALL is on the stack
;   - We need to figure out which hook this is (from return address - 5)
;   - Set up parameters and call executeLuaHook
;   - Then return, cleaning up based on original calling convention
;     (on x64, caller ALWAYS cleans up, so we just RET)
;
LuaLandingFromCpp_x64 PROC
    ; The CALL that brought us here pushed the return address
    ; Stack: [return_addr] [shadow] [arg0] [arg1] ...

    ; Save all volatile registers
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 32             ; shadow space for our calls

    ; Get the origin address: return_addr - 5 (size of CALL instruction)
    mov rax, [rsp + 32 + 7*8]  ; return address (past shadow + 7 pushes)
    sub rax, 5
    mov rcx, rax            ; arg1 = origin address
    mov rdx, r10            ; arg2 = live RCX value (was saved in R10 by trampoline setup)

    call SetLuaHookedFunctionParameters

    ; Calculate pointer to the original arguments on the stack
    ; Original args start after: shadow(32) + 7 pushes(56) + return_addr(8) + shadow(32)
    lea rcx, [rsp + 32 + 7*8 + 8 + 32]  ; pointer to original arg area
    call executeLuaHook

    ; Result in RAX - save it
    mov r10, rax

    add rsp, 32             ; remove our shadow space
    pop r11
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax

    ; Restore RAX with the hook return value
    mov rax, r10

    ret                     ; x64 caller always cleans up
LuaLandingFromCpp_x64 ENDP


; =============================================================================
; detourLandingFunction_x64
; =============================================================================
; Detour landing - saves all registers, calls the Lua detour handler,
; then restores registers and jumps to the return location.
;
detourLandingFunction_x64 PROC
    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    pushfq                  ; save flags

    sub rsp, 32             ; shadow space

    ; RCX = pointer to saved registers on stack
    lea rcx, [rsp + 32]     ; point past shadow space to the pushfq
    ; RDX = return address from the detour (origin)
    mov rdx, [rsp + 32 + 16*8]  ; past shadow + 16 pushes (15 regs + flags)
    sub rdx, 5              ; subtract CALL size to get origin

    call GetDetourLuaTargetAndCallTheLuaFunction

    add rsp, 32             ; remove shadow space

    ; Restore all registers
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove the return address pushed by the CALL
    add rsp, 8

    ; Jump to the return location set by GetDetourLuaTargetAndCallTheLuaFunction
    jmp QWORD PTR [currentDetourReturn]
detourLandingFunction_x64 ENDP


END
