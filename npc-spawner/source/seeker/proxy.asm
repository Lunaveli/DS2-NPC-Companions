option casemap:none
EXTERN EnsureReady:PROC
EXTERN originalExports:QWORD
.code
Proxy MACRO symbol, offset
symbol PROC
    sub rsp, 0A8h
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    call EnsureReady
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
    add rsp, 0A8h
    jmp QWORD PTR [originalExports+offset]
symbol ENDP
ENDM
Proxy ProxyCreate, 0
Proxy ProxyCanUnload, 8
Proxy ProxyGetClass, 16
Proxy ProxyRegister, 24
Proxy ProxyUnregister, 32
Proxy ProxyJoystick, 40
END
