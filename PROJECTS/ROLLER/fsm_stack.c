#include "fsm_stack.h"

FsmError fsm_stack_init(FsmStack *stack,
                        FsmMachine *machine,
                        int *storage,
                        int capacity)
{
  if (!stack || !machine || !storage || capacity <= 0)
    return FSM_ERROR_INVALID_ARGUMENT;

  stack->machine = machine;
  stack->states = storage;
  stack->capacity = capacity;
  stack->count = 0;
  return FSM_OK;
}

FsmError fsm_stack_push(FsmStack *stack, int overlay_state)
{
  FsmMachine *machine;

  if (!stack || !stack->machine || !stack->states)
    return FSM_ERROR_INVALID_ARGUMENT;

  machine = stack->machine;
  if (!fsm_state_is_valid(machine, overlay_state))
    return FSM_ERROR_INVALID_STATE;

  if (stack->count >= stack->capacity)
    return FSM_ERROR_STACK_FULL;

  if (!fsm_state_is_valid(machine, machine->current_state))
    return FSM_ERROR_INVALID_STATE;

  stack->states[stack->count++] = machine->current_state;
  machine->current_state = overlay_state;
  machine->pending_state = overlay_state;

  return fsm_enter_current_state(machine);
}

FsmError fsm_stack_pop(FsmStack *stack)
{
  FsmMachine *machine;
  FsmError exit_error;
  int restored_state;

  if (!stack || !stack->machine || !stack->states)
    return FSM_ERROR_INVALID_ARGUMENT;

  if (stack->count <= 0)
    return FSM_ERROR_STACK_EMPTY;

  machine = stack->machine;
  if (!fsm_state_is_valid(machine, machine->current_state))
    return FSM_ERROR_INVALID_STATE;

  exit_error = fsm_exit_current_state(machine);
  if (exit_error != FSM_OK)
    return exit_error;

  restored_state = stack->states[--stack->count];
  if (!fsm_state_is_valid(machine, restored_state))
    return FSM_ERROR_INVALID_STATE;

  machine->current_state = restored_state;
  machine->pending_state = restored_state;
  return FSM_OK;
}

int fsm_stack_count(const FsmStack *stack)
{
  return stack ? stack->count : 0;
}

