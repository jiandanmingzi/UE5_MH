; Test-only nested frames, matching only the independently verified stack shape.
; No game address, memory read, or game process interaction.
option casemap:none
EXTERN MhgzTriggerEntry:PROC
PUBLIC MhgzTriggerFixture
PUBLIC MhgzTestSpatialReturn
PUBLIC MhgzTestHitReturn
.code
MhgzTriggerFixture PROC FRAME
    push rdi
    .pushreg rdi
    push r14
    .pushreg r14
    sub rsp,28h
    .allocstack 28h
    .endprolog
    mov rdi,0fedcba9812345678h
    mov r14,0123456789abcdef0h
    call FixtureSpatial
MhgzTestHitReturn LABEL NEAR
    add rsp,28h
    pop r14
    pop rdi
    ret
MhgzTriggerFixture ENDP
FixtureSpatial PROC FRAME
    push rdi
    .pushreg rdi
    sub rsp,80h
    .allocstack 80h
    .endprolog
    call MhgzTriggerEntry
MhgzTestSpatialReturn LABEL NEAR
    add rsp,80h
    pop rdi
    ret
FixtureSpatial ENDP
END
