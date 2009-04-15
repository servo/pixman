    area pixman_msvc, code, readonly

    export  pixman_msvc_try_armv6_op

pixman_msvc_try_armv6_op
    uqadd8 r0,r0,r1
    mov pc,lr
    endp

    end
