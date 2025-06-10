# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	

# 这里非常巧妙 只记录了当前swtch函数内ra的值 而没有记录pc
# 这是因为当外部函数调用swtch这个方法的时候 会自动的将当前运行的ra设置为原来进程的pc
# so that运行完swtch 才会知道当前函数应该跳回哪个地方去（出栈的位置）
# 而刷新这个新ra的步骤 是在调用编译后的swtch函数时，所对应的call语句  “call swtch     # == jal ra, swtch”

# 总的来说 就是利用了swtch需要返回的一个天然机制 记录当前的（swtch外的原线程）pc

.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret

	
