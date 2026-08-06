#include "fsm.h"

static void fsm_clear(FsmMachine *machine)
{
  if (!machine)
    return;

  machine->states = 0;
  machine->uniform_state.enter = 0;
  machine->uniform_state.update = 0;
  machine->uniform_state.draw = 0;
  machine->uniform_state.exit = 0;
  machine->state_count = 0;
  machine->current_state = -1;
  machine->pending_state = -1;
  machine->context = 0;
}

static FsmStepResult fsm_empty_result(FsmError error)
{
  FsmStepResult result = { 0, -1, -1, error };
  return result;
}

int fsm_state_is_valid(const FsmMachine *machine, int state)
{
  return machine && machine->state_count > 0 &&
         state >= 0 && state < machine->state_count;
}

static const FsmState *fsm_state_at(const FsmMachine *machine, int state)
{
  if (!fsm_state_is_valid(machine, state))
    return 0;

  if (machine->states)
    return &machine->states[state];

  return &machine->uniform_state;
}

static FsmStepResult fsm_apply_transition(FsmMachine *machine, int target_state)
{
  FsmStepResult result;
  const FsmState *old_state;
  const FsmState *new_state;

  if (!machine)
    return fsm_empty_result(FSM_ERROR_INVALID_ARGUMENT);

  old_state = fsm_state_at(machine, machine->current_state);
  new_state = fsm_state_at(machine, target_state);
  if (!old_state || !new_state)
    return fsm_empty_result(FSM_ERROR_INVALID_STATE);

  result = fsm_empty_result(FSM_OK);
  if (target_state == machine->current_state) {
    machine->pending_state = target_state;
    return result;
  }

  result.transitioned = 1;
  result.from_state = machine->current_state;
  result.to_state = target_state;

  if (old_state->exit)
    old_state->exit(machine->context);

  machine->current_state = target_state;
  machine->pending_state = target_state;

  if (new_state->enter)
    new_state->enter(machine->context);

  return result;
}

static FsmError fsm_init_common(FsmMachine *machine,
                                const FsmState *states,
                                const FsmState *uniform_state,
                                int state_count,
                                int initial_state,
                                void *context)
{
  const FsmState *initial;

  if (!machine)
    return FSM_ERROR_INVALID_ARGUMENT;

  fsm_clear(machine);

  if ((!states && !uniform_state) || state_count <= 0)
    return FSM_ERROR_INVALID_ARGUMENT;

  if (initial_state < 0 || initial_state >= state_count)
    return FSM_ERROR_INVALID_STATE;

  machine->states = states;
  if (uniform_state)
    machine->uniform_state = *uniform_state;
  machine->state_count = state_count;
  machine->current_state = initial_state;
  machine->pending_state = initial_state;
  machine->context = context;

  initial = fsm_state_at(machine, initial_state);
  if (initial && initial->enter)
    initial->enter(context);

  return FSM_OK;
}

FsmError fsm_init(FsmMachine *machine,
                  const FsmState *states,
                  int state_count,
                  int initial_state,
                  void *context)
{
  return fsm_init_common(machine, states, 0, state_count, initial_state, context);
}

FsmError fsm_init_uniform(FsmMachine *machine,
                          const FsmState *state,
                          int state_count,
                          int initial_state,
                          void *context)
{
  return fsm_init_common(machine, 0, state, state_count, initial_state, context);
}

FsmError fsm_request(FsmMachine *machine, int target_state)
{
  if (!machine)
    return FSM_ERROR_INVALID_ARGUMENT;

  if (!fsm_state_is_valid(machine, target_state))
    return FSM_ERROR_INVALID_STATE;

  machine->pending_state = target_state;
  return FSM_OK;
}

FsmStepResult fsm_transition(FsmMachine *machine, int target_state)
{
  return fsm_apply_transition(machine, target_state);
}

FsmError fsm_enter_current_state(FsmMachine *machine)
{
  const FsmState *state;

  if (!machine)
    return FSM_ERROR_INVALID_ARGUMENT;

  state = fsm_state_at(machine, machine->current_state);
  if (!state)
    return FSM_ERROR_INVALID_STATE;

  if (state->enter)
    state->enter(machine->context);

  return FSM_OK;
}

FsmError fsm_exit_current_state(FsmMachine *machine)
{
  const FsmState *state;

  if (!machine)
    return FSM_ERROR_INVALID_ARGUMENT;

  state = fsm_state_at(machine, machine->current_state);
  if (!state)
    return FSM_ERROR_INVALID_STATE;

  if (state->exit)
    state->exit(machine->context);

  return FSM_OK;
}

FsmStepResult fsm_step(FsmMachine *machine)
{
  FsmStepResult result = { 0, -1, -1, FSM_OK };
  const FsmState *state;

  if (!machine) {
    result.error = FSM_ERROR_INVALID_ARGUMENT;
    return result;
  }

  if (!fsm_state_is_valid(machine, machine->current_state) ||
      !fsm_state_is_valid(machine, machine->pending_state)) {
    result.error = FSM_ERROR_INVALID_STATE;
    return result;
  }

  if (machine->pending_state != machine->current_state) {
    result = fsm_apply_transition(machine, machine->pending_state);
    if (result.error != FSM_OK)
      return result;
  }

  state = fsm_state_at(machine, machine->current_state);
  if (state && state->update)
    state->update(machine->context);

  state = fsm_state_at(machine, machine->current_state);
  if (machine->pending_state == machine->current_state && state && state->draw)
    state->draw(machine->context);

  return result;
}

int fsm_current(const FsmMachine *machine)
{
  return machine ? machine->current_state : -1;
}

int fsm_pending(const FsmMachine *machine)
{
  return machine ? machine->pending_state : -1;
}
