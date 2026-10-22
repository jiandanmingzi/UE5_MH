; x64 entry observer. Preserve the complete original register/flag state and
; original stack arguments; do not guess PostEvent's total parameter count.
; The callback reads only event ID (RCX), game object ID (RDX), flags (R8).
option casemap:none
EXTERN MhgzObserveEvent:PROC
EXTERN MhgzOriginalPost:QWORD
EXTERN MhgzObserveState:PROC
EXTERN MhgzObserveSwitch:PROC
EXTERN MhgzObserveRtpc:PROC
EXTERN MhgzObserveReset:PROC
EXTERN MhgzObserveRegister:PROC
EXTERN MhgzObserveUnregister:PROC
EXTERN MhgzOriginalState:QWORD
EXTERN MhgzOriginalSwitch:QWORD
EXTERN MhgzOriginalRtpc:QWORD
EXTERN MhgzOriginalReset:QWORD
EXTERN MhgzOriginalRegister:QWORD
EXTERN MhgzOriginalUnregister:QWORD
EXTERN MhgzObserveTrigger:PROC
EXTERN MhgzOriginalTrigger:QWORD
EXTERN MhgzSpatialReturn:QWORD
EXTERN MhgzHitReturnA:QWORD
EXTERN MhgzHitReturnB:QWORD
.code
; Shared transparent entry. RTPC additionally copies its two verified stack
; scalars to the observer's own outgoing area; original arguments are untouched.
OBSERVE_ENTRY MACRO entryPoint, observer, original, rtpcStack:=<0>, triggerFrame:=<0>
PUBLIC entryPoint
entryPoint PROC FRAME
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    sub rsp, 0a8h
    .allocstack 0e8h
    .endprolog
    movdqu xmmword ptr [rsp+040h], xmm0
    movdqu xmmword ptr [rsp+050h], xmm1
    movdqu xmmword ptr [rsp+060h], xmm2
    movdqu xmmword ptr [rsp+070h], xmm3
    movdqu xmmword ptr [rsp+080h], xmm4
    movdqu xmmword ptr [rsp+090h], xmm5
    IF rtpcStack
        mov eax, dword ptr [rsp+110h]
        mov dword ptr [rsp+020h], eax
        movzx eax, byte ptr [rsp+118h]
        mov dword ptr [rsp+028h], eax
    ENDIF
    IF triggerFrame
        ; Optional verified caller chain. At the spatial caller: push rdi,
        ; sub rsp,80h, call common submit. Original RDI is the managed HitInfo;
        ; R14 is its thread context. Do NOT read extra stack slots on other callers.
        xor eax, eax
        mov qword ptr [rsp+020h], rax
        mov qword ptr [rsp+028h], rax
        mov qword ptr [rsp+030h], rax
        mov rax, qword ptr [rsp+0e8h]
        cmp rax, qword ptr [MhgzSpatialReturn]
        jne no_hit_context
        mov rax, qword ptr [rsp+178h]
        cmp rax, qword ptr [MhgzHitReturnA]
        je capture_hit_context
        cmp rax, qword ptr [MhgzHitReturnB]
        jne no_hit_context
    capture_hit_context:
        mov qword ptr [rsp+030h], rax
        mov rax, qword ptr [rsp+170h]
        mov qword ptr [rsp+020h], rax
        mov qword ptr [rsp+028h], r14
    no_hit_context:
    ENDIF
    call observer
    movdqu xmm0, xmmword ptr [rsp+040h]
    movdqu xmm1, xmmword ptr [rsp+050h]
    movdqu xmm2, xmmword ptr [rsp+060h]
    movdqu xmm3, xmmword ptr [rsp+070h]
    movdqu xmm4, xmmword ptr [rsp+080h]
    movdqu xmm5, xmmword ptr [rsp+090h]
    add rsp, 0a8h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [original]
entryPoint ENDP
ENDM
OBSERVE_ENTRY MhgzPostEntry, MhgzObserveEvent, MhgzOriginalPost
OBSERVE_ENTRY MhgzStateEntry, MhgzObserveState, MhgzOriginalState
OBSERVE_ENTRY MhgzSwitchEntry, MhgzObserveSwitch, MhgzOriginalSwitch
OBSERVE_ENTRY MhgzRtpcEntry, MhgzObserveRtpc, MhgzOriginalRtpc, 1
OBSERVE_ENTRY MhgzResetEntry, MhgzObserveReset, MhgzOriginalReset
OBSERVE_ENTRY MhgzRegisterEntry, MhgzObserveRegister, MhgzOriginalRegister
OBSERVE_ENTRY MhgzUnregisterEntry, MhgzObserveUnregister, MhgzOriginalUnregister
OBSERVE_ENTRY MhgzTriggerEntry, MhgzObserveTrigger, MhgzOriginalTrigger, 0, 1
END
