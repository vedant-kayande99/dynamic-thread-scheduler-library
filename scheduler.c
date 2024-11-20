#undef _FORTIFY_SOURCE
#define STACK_SIZE 1048576
#define ALRM_SECONDS 1

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

struct thread
{
    jmp_buf ctx;
    struct
    {
        char *memory;
        char *_memory;
    } stack;
    struct
    {
        void *arg;
        scheduler_fnc_t fnc;
    } code;
    enum
    {
        INIT,
        RUNNING,
        SLEEPING,
        TERMINATED
    } status;

    struct thread *next;
};

jmp_buf global_ctx;
struct thread *head = NULL;
struct thread *cur_thread = NULL;

int scheduler_create(scheduler_fnc_t fnc, void *arg)
{
    struct thread *t = malloc(sizeof(struct thread));
    t->status = INIT;
    t->code.arg = arg;
    t->code.fnc = fnc;
    t->stack._memory = malloc(STACK_SIZE + page_size());
    t->stack.memory = memory_align(t->stack._memory, page_size());
    t->next = head;
    head = t;
    return 0;
}

void destroy(void)
{
    struct thread *t = head;
    while (t != NULL)
    {
        struct thread *prev = t;
        t = t->next;
        if (prev->stack._memory != NULL)
        {
            free(prev->stack._memory);
            prev->stack._memory = NULL;
        }
        free(prev);
    }
    head = NULL;
    cur_thread = NULL;
}

void scheduler_yield()
{

    if (!setjmp(cur_thread->ctx))
    {
        cur_thread->status = SLEEPING;
        longjmp(global_ctx, 1);
    }
}

static struct thread *candidate(void)
{
    struct thread *t;
    if (NULL == cur_thread)
    {
        t = head;
    }
    else
    {
        t = cur_thread->next ? cur_thread->next : head;
    }
    if (NULL == t)
    {
        TRACE("No threads created");
        return NULL;
    }
    while (t->status == TERMINATED)
    {
        if (t->next == NULL)
        {
            t = head;
        }
        else
        {
            t = t->next;
        }
        if (t == cur_thread)
        {
            return NULL;
        }
    }
    return t;
}

static void schedule(void)
{
    struct thread *t = candidate();
    if (NULL == t)
    {
        return;
    }
    cur_thread = t;
    if (cur_thread->status == SLEEPING)
    {
        cur_thread->status = RUNNING;
        longjmp(cur_thread->ctx, 1);
    }
    else
    {
        __asm__ volatile(
            "mov %[rs], %%rsp\n"
            :
            : [rs] "r"(cur_thread->stack.memory + STACK_SIZE)
            :);
        cur_thread->status = RUNNING;
        cur_thread->code.fnc(cur_thread->code.arg);
        cur_thread->status = TERMINATED;
        longjmp(global_ctx, 1);
    }
}

void scheduler_execute(void)
{
    set_signal_handler();
    setjmp(global_ctx);
    schedule();
    destroy();
}

void alarm_handler(int signum)
{
    UNUSED(signum);
    set_signal_handler();
    scheduler_yield();
}

void set_signal_handler()
{
    if ((signal(SIGALRM, alarm_handler)) == SIG_ERR)
    {
        TRACE("Error catching signal");
    }
    alarm(ALRM_SECONDS);
}
