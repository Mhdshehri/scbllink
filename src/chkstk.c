/* chkstk.c - the stack probe clang calls (as _alloca) before a function whose
 * locals are bigger than a page. Windows grows a thread's stack one guard page
 * at a time, so such a function must touch each page in order, or it crashes.
 * There is no C runtime in SCBLLink's builds to provide it, so here it is:
 * EAX = bytes wanted; touches each 4 KB page going down, then moves ESP down by
 * EAX and returns. Used by SCBLLink.exe and by its game DLL. By Mhdshehri. */

__asm__(
    ".text\n"
    ".globl __alloca\n"
    "__alloca:\n"
    "    pushl %ecx\n"
    "    leal 8(%esp), %ecx\n"      /* the stack as the caller had it */
    "    cmpl $0x1000, %eax\n"
    "    jb 2f\n"
    "1:  subl $0x1000, %ecx\n"
    "    orl $0, (%ecx)\n"          /* touch: the guard page moves down */
    "    subl $0x1000, %eax\n"
    "    cmpl $0x1000, %eax\n"
    "    ja 1b\n"
    "2:  subl %eax, %ecx\n"
    "    orl $0, (%ecx)\n"
    "    movl %esp, %eax\n"
    "    movl %ecx, %esp\n"         /* the caller's locals now fit */
    "    movl (%eax), %ecx\n"       /* ECX back */
    "    pushl 4(%eax)\n"           /* and back to the caller */
    "    ret\n");
