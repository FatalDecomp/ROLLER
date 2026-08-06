#ifndef ROLLER_FSM_STACK_H
#define ROLLER_FSM_STACK_H

#include "fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  FsmMachine *machine;
  int *states;
  int capacity;
  int count;
} FsmStack;

FsmError fsm_stack_init(FsmStack *stack,
                        FsmMachine *machine,
                        int *storage,
                        int capacity);
FsmError fsm_stack_push(FsmStack *stack, int overlay_state);
FsmError fsm_stack_pop(FsmStack *stack);
int fsm_stack_count(const FsmStack *stack);

#ifdef __cplusplus
}
#endif

#endif
